#include "kota/http/detail/manager.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../async/libuv.h"
#include "kota/http/detail/runtime.h"

namespace kota::http {

namespace {

using manager_table = std::unordered_map<event_loop*, std::unique_ptr<manager>>;

auto& table_mutex() {
    static std::mutex lock;
    return lock;
}

auto& managers() {
    static manager_table table;
    return table;
}

}  // namespace

struct manager::timer_context {
    uv_timer_t handle{};
    manager* owner = nullptr;
};

struct manager::socket_context {
    uv_poll_t handle{};
    curl_socket_t socket = 0;
    manager* owner = nullptr;
};

manager::manager(event_loop& loop, curl::multi_handle handle) noexcept :
    bound_loop(&loop), multi(std::move(handle)) {
    timer = new timer_context();
    timer->owner = this;
    uv::timer_init(loop, timer->handle);
    timer->handle.data = timer;
}

std::expected<void, error> manager::initialize() {
    if(auto err =
           curl::multi_setopt(multi.get(), CURLMOPT_SOCKETFUNCTION, &manager::on_curl_socket);
       !curl::ok(err)) {
        return std::unexpected(error::from_curl(
            err,
            std::format("failed to register curl socket callback: {}", curl::message(err))));
    }

    if(auto err = curl::multi_setopt(multi.get(), CURLMOPT_SOCKETDATA, static_cast<void*>(this));
       !curl::ok(err)) {
        return std::unexpected(error::from_curl(
            err,
            std::format("failed to register curl socket callback data: {}", curl::message(err))));
    }

    if(auto err =
           curl::multi_setopt(multi.get(), CURLMOPT_TIMERFUNCTION, &manager::on_curl_timeout);
       !curl::ok(err)) {
        return std::unexpected(error::from_curl(
            err,
            std::format("failed to register curl timer callback: {}", curl::message(err))));
    }

    if(auto err = curl::multi_setopt(multi.get(), CURLMOPT_TIMERDATA, static_cast<void*>(this));
       !curl::ok(err)) {
        return std::unexpected(error::from_curl(
            err,
            std::format("failed to register curl timer callback data: {}", curl::message(err))));
    }

    return {};
}

manager::~manager() {
    close_watchers();
    if(multi) {
        [[maybe_unused]] auto socket_cb =
            curl::multi_setopt(multi.get(), CURLMOPT_SOCKETFUNCTION, nullptr);
        [[maybe_unused]] auto socket_data =
            curl::multi_setopt(multi.get(), CURLMOPT_SOCKETDATA, nullptr);
        [[maybe_unused]] auto timer_cb =
            curl::multi_setopt(multi.get(), CURLMOPT_TIMERFUNCTION, nullptr);
        [[maybe_unused]] auto timer_data =
            curl::multi_setopt(multi.get(), CURLMOPT_TIMERDATA, nullptr);
        multi.reset();
    }
}

std::expected<std::reference_wrapper<manager>, error> manager::try_for_loop(event_loop& loop) {
    if(auto code = detail::ensure_curl_runtime(); !curl::ok(code)) {
        return std::unexpected(error::from_curl(code));
    }

    std::scoped_lock lock(table_mutex());
    auto& table = managers();

    if(auto it = table.find(&loop); it != table.end()) {
        return *it->second;
    }

    auto multi = curl::multi_handle::create();
    if(!multi) {
        return std::unexpected(
            error::from_curl(CURLE_FAILED_INIT, "failed to create curl multi handle"));
    }

    auto created = std::unique_ptr<manager>(new manager(loop, std::move(multi)));
    if(auto init = created->initialize(); !init) {
        return std::unexpected(std::move(init.error()));
    }

    auto& out = *created;
    table.emplace(&loop, std::move(created));
    return out;
}

manager& manager::for_loop(event_loop& loop) {
    auto out = try_for_loop(loop);
    if(!out) {
        std::abort();
    }
    return out->get();
}

void manager::unregister_loop(event_loop& loop) {
    std::scoped_lock lock(table_mutex());
    managers().erase(&loop);
}

curl::multi_error manager::add_request(CURL* easy) noexcept {
    auto err = curl::multi_add_handle(multi.get(), easy);
    if(curl::ok(err)) {
        active_requests += 1;
    }
    return err;
}

curl::multi_error manager::remove_request(CURL* easy) noexcept {
    auto err = curl::multi_remove_handle(multi.get(), easy);
    if(curl::ok(err) && active_requests > 0) {
        active_requests -= 1;
    }
    return err;
}

void manager::drive_timeout() noexcept {
    int running = 0;
    [[maybe_unused]] auto err =
        curl::multi_socket_action(multi.get(), CURL_SOCKET_TIMEOUT, 0, &running);
    drain_completed();
}

void manager::drive_timeout_arming(void* arming_request) noexcept {
    int running = 0;
    [[maybe_unused]] auto err =
        curl::multi_socket_action(multi.get(), CURL_SOCKET_TIMEOUT, 0, &running);
    drain_completed_arming(arming_request);
}

void manager::drive_socket(curl_socket_t socket, int flags) noexcept {
    int running = 0;
    [[maybe_unused]] auto err = curl::multi_socket_action(multi.get(), socket, flags, &running);
    drain_completed();
}

void manager::drain_completed() noexcept {
    drain_completed_impl(nullptr);
}

void manager::drain_completed_arming(void* arming_request) noexcept {
    drain_completed_impl(arming_request);
}

void manager::drain_completed_impl(void* arming_request) noexcept {
    std::vector<std::pair<detail::inflight_request_ref, curl::easy_error>> deferred;
    int pending = 0;
    while(auto* msg = curl::multi_info_read(multi.get(), &pending)) {
        if(msg->msg != CURLMSG_DONE) {
            continue;
        }

        auto* easy = msg->easy_handle;
        void* opaque = nullptr;
        if(auto err = curl::getinfo(easy, CURLINFO_PRIVATE, &opaque); !curl::ok(err) || !opaque) {
            continue;
        }

        auto request = detail::retain_inflight_request(opaque);
        if(!request) {
            continue;
        }

        remove_request(easy);
        if(arming_request != nullptr && opaque == arming_request) {
            detail::mark_inflight_request_removed(request);
            detail::complete_inflight_request(request, msg->data.result, true);
            continue;
        }

        detail::mark_inflight_request_removed(request);
        deferred.emplace_back(std::move(request), msg->data.result);
    }

    if(deferred.empty()) {
        return;
    }

    for(auto& [request, result]: deferred) {
        detail::complete_inflight_request(request, result, false);
    }
}

manager::socket_context* manager::ensure_socket(curl_socket_t socket) noexcept {
    if(auto it = sockets.find(socket); it != sockets.end()) {
        return it->second;
    }

    auto* context = new socket_context();
    context->socket = socket;
    context->owner = this;

    if(auto err = uv::poll_init_socket(loop(), context->handle, static_cast<uv_os_sock_t>(socket));
       err) {
        delete context;
        return nullptr;
    }

    context->handle.data = context;
    sockets.emplace(socket, context);
    return context;
}

void manager::update_socket(curl_socket_t socket, int action, void* socketp) noexcept {
    switch(action) {
        case CURL_POLL_IN:
        case CURL_POLL_OUT:
        case CURL_POLL_INOUT: {
            auto* context = socketp ? static_cast<socket_context*>(socketp) : ensure_socket(socket);
            if(!context) {
                return;
            }

            [[maybe_unused]] auto assign =
                curl::multi_assign(multi.get(), socket, static_cast<void*>(context));

            int events = 0;
            if(action != CURL_POLL_IN) {
                events |= UV_WRITABLE;
            }
            if(action != CURL_POLL_OUT) {
                events |= UV_READABLE;
            }

            [[maybe_unused]] auto err =
                uv::poll_start(context->handle, events, &manager::on_uv_socket);
            return;
        }

        case CURL_POLL_REMOVE: {
            auto* context = socketp ? static_cast<socket_context*>(socketp) : nullptr;
            if(!context) {
                if(auto it = sockets.find(socket); it != sockets.end()) {
                    context = it->second;
                }
            }
            if(!context) {
                return;
            }

            sockets.erase(context->socket);
            context->owner = nullptr;
            [[maybe_unused]] auto err = uv::poll_stop(context->handle);
            [[maybe_unused]] auto assign = curl::multi_assign(multi.get(), socket, nullptr);
            if(!uv::is_closing(context->handle)) {
                uv::close(context->handle, &manager::on_uv_socket_close);
            }
            return;
        }

        default: return;
    }
}

void manager::update_timeout(long timeout_ms) noexcept {
    if(!timer) {
        return;
    }

    if(timeout_ms < 0) {
        uv::timer_stop(timer->handle);
        return;
    }

    if(timeout_ms == 0) {
        timeout_ms = 1;
    }

    uv::timer_start(timer->handle,
                    &manager::on_uv_timeout,
                    static_cast<std::uint64_t>(timeout_ms),
                    0);
}

void manager::close_watchers() noexcept {
    for([[maybe_unused]] auto& [socket, context]: sockets) {
        if(!context) {
            continue;
        }
        context->owner = nullptr;
        [[maybe_unused]] auto stop = uv::poll_stop(context->handle);
        [[maybe_unused]] auto assign = curl::multi_assign(multi.get(), context->socket, nullptr);
        if(!uv::is_closing(context->handle)) {
            uv::close(context->handle, &manager::on_uv_socket_close);
        }
    }
    sockets.clear();

    if(timer) {
        timer->owner = nullptr;
        uv::timer_stop(timer->handle);
        if(!uv::is_closing(timer->handle)) {
            uv::close(timer->handle, &manager::on_uv_timer_close);
        }
        timer = nullptr;
    }
}

int manager::on_curl_socket(CURL*,
                            curl_socket_t socket,
                            int action,
                            void* userp,
                            void* socketp) noexcept {
    auto* self = static_cast<manager*>(userp);
    if(!self) {
        return 0;
    }

    self->update_socket(socket, action, socketp);
    return 0;
}

int manager::on_curl_timeout(CURLM*, long timeout_ms, void* userp) noexcept {
    auto* self = static_cast<manager*>(userp);
    if(!self) {
        return 0;
    }

    self->update_timeout(timeout_ms);
    return 0;
}

void manager::on_uv_socket(uv_poll_t* handle, int status, int events) noexcept {
    auto* context = static_cast<socket_context*>(handle->data);
    if(!context || !context->owner) {
        return;
    }

    int flags = 0;
    if(status < 0) {
        flags |= CURL_CSELECT_ERR;
    }
    if(events & UV_READABLE) {
        flags |= CURL_CSELECT_IN;
    }
    if(events & UV_WRITABLE) {
        flags |= CURL_CSELECT_OUT;
    }

    context->owner->drive_socket(context->socket, flags);
}

void manager::on_uv_timeout(uv_timer_t* handle) noexcept {
    auto* context = static_cast<timer_context*>(handle->data);
    if(!context || !context->owner) {
        return;
    }

    context->owner->drive_timeout();
}

void manager::on_uv_socket_close(uv_handle_t* handle) noexcept {
    auto* context = static_cast<socket_context*>(handle->data);
    delete context;
}

void manager::on_uv_timer_close(uv_handle_t* handle) noexcept {
    auto* context = static_cast<timer_context*>(handle->data);
    delete context;
}

}  // namespace kota::http
