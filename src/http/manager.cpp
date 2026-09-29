#include "kota/http/detail/manager.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "transfer.h"
#include "../async/libuv.h"

namespace kota::http {

namespace {

/// The manager of each loop that has sent a request. A loop keeps its
/// entry until it is destroyed; unregister_loop() empties it.
using manager_table = std::unordered_map<event_loop*, std::unique_ptr<manager>>;

std::mutex& table_mutex() {
    static std::mutex lock;
    return lock;
}

manager_table& managers() {
    static manager_table table;
    return table;
}

/// Initializes libcurl once for the process, and cleans it up at exit.
curl::easy_error curl_runtime() noexcept {
    struct runtime {
        runtime() noexcept : code(curl::global_init()) {}

        ~runtime() {
            if(curl::ok(code)) {
                curl::global_cleanup();
            }
        }

        curl::easy_error code;
    };

    static runtime state;
    return state.code;
}

}  // namespace

struct manager::timer_watch : uv::owned_handle<timer_watch> {
    union {
        uv_handle_t handle;
        uv_timer_t timer;
    };

    manager* owner = nullptr;

    static void on_fire(uv_timer_t* timer) {
        static_cast<timer_watch*>(timer->data)->owner->drive(CURL_SOCKET_TIMEOUT, 0);
    }
};

struct manager::socket_watch : uv::owned_handle<socket_watch> {
    union {
        uv_handle_t handle;
        uv_poll_t poll;
    };

    manager* owner = nullptr;
    curl_socket_t socket = CURL_SOCKET_BAD;

    static void on_events(uv_poll_t* poll, int status, int events) {
        auto& watch = *static_cast<socket_watch*>(poll->data);
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
        watch.owner->drive(watch.socket, flags);
    }
};

manager::manager(event_loop& loop, curl::multi_handle multi) noexcept :
    bound_loop(&loop), multi(std::move(multi)), timer(timer_watch::make()) {
    ::uv_timer_init(loop.native_handle(), &timer->timer);
    timer->owner = this;
    auto* handle = this->multi.get();
    curl::multi_setopt(handle,
                       CURLMOPT_SOCKETFUNCTION,
                       static_cast<curl_socket_callback>(&manager::on_socket));
    curl::multi_setopt(handle, CURLMOPT_SOCKETDATA, static_cast<void*>(this));
    curl::multi_setopt(handle,
                       CURLMOPT_TIMERFUNCTION,
                       static_cast<curl_multi_timer_callback>(&manager::on_timeout));
    curl::multi_setopt(handle, CURLMOPT_TIMERDATA, static_cast<void*>(this));
}

manager::~manager() {
    // Nothing curl does from here on reaches this manager, and nothing is
    // polled for it any more.
    auto* handle = multi.get();
    curl::multi_setopt(handle, CURLMOPT_SOCKETFUNCTION, nullptr);
    curl::multi_setopt(handle, CURLMOPT_TIMERFUNCTION, nullptr);
    sockets.clear();
    timer.reset();

    auto& loop = *bound_loop->native_handle();
    const bool dying = uv::destroying(loop);
    while(transfers != nullptr) {
        auto& job = *transfers;
        const bool driven = !job.queued;
        drop(job);
        if(dying) {
            // The loop completes nothing queued any more: the cancel of the
            // task that waits ends it, as it does one that was never queued.
            job.queued = false;
        } else if(driven) {
            job.failure = error::aborted("the event loop's http manager was destroyed");
            job.queued = true;
            uv::complete_later(loop, job);
        }
    }
}

std::expected<std::reference_wrapper<manager>, error> manager::try_for_loop(event_loop& loop) {
    // A manager made now would outlive the loop, which would never close its
    // timer.
    if(uv::destroying(*loop.native_handle())) {
        return std::unexpected(error::aborted("the event loop is being destroyed"));
    }
    if(auto code = curl_runtime(); !curl::ok(code)) {
        return std::unexpected(error::from_curl(code, "failed to initialize libcurl"));
    }

    std::scoped_lock lock(table_mutex());
    auto [entry, first] = managers().try_emplace(&loop);
    if(first) {
        // The entry goes with the loop, so that a loop made later at the same
        // address starts afresh.
        loop.on_destroy([&loop] {
            std::unique_ptr<manager> gone;
            std::scoped_lock lock(table_mutex());
            gone = std::move(managers().extract(&loop).mapped());
        });
    }
    if(!entry->second) {
        auto multi = curl::multi_handle::create();
        if(!multi) {
            return std::unexpected(
                error::from_curl(CURLE_FAILED_INIT, "failed to create curl multi handle"));
        }
        entry->second.reset(new manager(loop, std::move(multi)));
    }
    return *entry->second;
}

manager& manager::for_loop(event_loop& loop) {
    auto found = try_for_loop(loop);
    if(!found) {
        std::fprintf(stderr, "fatal: %s\n", found.error().message().c_str());
        std::abort();
    }
    return *found;
}

void manager::unregister_loop(event_loop& loop) {
    std::unique_ptr<manager> gone;
    std::scoped_lock lock(table_mutex());
    if(auto entry = managers().find(&loop); entry != managers().end()) {
        gone = std::move(entry->second);
    }
}

std::size_t manager::pending_requests() const noexcept {
    std::size_t driven = 0;
    for(auto* job = transfers; job != nullptr; job = job->next) {
        driven += job->queued ? 0 : 1;
    }
    return driven;
}

std::expected<std::shared_ptr<curl::share_handle>, error>
    manager::share_for(const std::shared_ptr<const detail::share_key>& key) {
    if(auto found = jars.find(key); found != jars.end()) {
        return found->second;
    }
    // The shares of the clients that have gone go before a new one comes.
    std::erase_if(jars, [](const auto& entry) { return entry.first.expired(); });

    auto share = std::make_shared<curl::share_handle>(curl::share_handle::create());
    if(!*share) {
        return std::unexpected(
            error::from_curl(CURLE_FAILED_INIT, "failed to create curl share handle"));
    }
    for(auto data: {CURL_LOCK_DATA_COOKIE, CURL_LOCK_DATA_DNS}) {
        if(auto err = curl::share_setopt(share->get(), CURLSHOPT_SHARE, data); !curl::ok(err)) {
            return std::unexpected(error::from_curl(err, std::string(curl::message(err))));
        }
    }
    // Only a curl built with TLS shares its sessions.
    curl::share_setopt(share->get(), CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);

    jars.emplace(key, share);
    return share;
}

curl::multi_error manager::add(detail::transfer& job) noexcept {
    auto err = curl::multi_add_handle(multi.get(), job.easy.get());
    if(curl::ok(err)) {
        job.next = transfers;
        if(transfers != nullptr) {
            transfers->prev = &job;
        }
        transfers = &job;
    }
    return err;
}

void manager::drop(detail::transfer& job) noexcept {
    if(!job.queued) {
        curl::multi_remove_handle(multi.get(), job.easy.get());
    }
    if(job.prev != nullptr) {
        job.prev->next = job.next;
    } else {
        transfers = job.next;
    }
    if(job.next != nullptr) {
        job.next->prev = job.prev;
    }
    job.prev = nullptr;
    job.next = nullptr;
    job.owner = nullptr;
}

void manager::drive(curl_socket_t socket, int events) noexcept {
    int running = 0;
    curl::multi_socket_action(multi.get(), socket, events, &running);

    int left = 0;
    while(auto* message = curl::multi_info_read(multi.get(), &left)) {
        assert(message->msg == CURLMSG_DONE && "curl reports finished transfers only");
        void* found = nullptr;
        curl::getinfo(message->easy_handle, CURLINFO_PRIVATE, &found);
        auto& job = *static_cast<detail::transfer*>(found);
        // Taking the handle from curl frees the message.
        if(auto code = message->data.result; code != CURLE_OK) {
            job.failure = error::from_curl(code);
        }
        curl::multi_remove_handle(multi.get(), job.easy.get());
        job.queued = true;
        uv::complete_later(*bound_loop->native_handle(), job);
    }
}

int manager::watch_socket(curl_socket_t socket, int what, socket_watch* watch) noexcept {
    // curl forgets the watch as this returns. It removes only the sockets it
    // has announced, and only a socket that got a watch counts as announced.
    if(what == CURL_POLL_REMOVE) {
        sockets.erase(socket);
        return 0;
    }

    if(watch == nullptr) {
        auto made = socket_watch::make();
        made->owner = this;
        made->socket = socket;
        // On failure, `made` goes with the handle, closed if libuv listed it.
        if(::uv_poll_init_socket(bound_loop->native_handle(),
                                 &made->poll,
                                 static_cast<uv_os_sock_t>(socket)) != 0) {
            return -1;
        }
        watch = made.get();
        curl::multi_assign(multi.get(), socket, watch);
        sockets.emplace(socket, std::move(made));
    }

    int events = 0;
    if(what & CURL_POLL_IN) {
        events |= UV_READABLE;
    }
    if(what & CURL_POLL_OUT) {
        events |= UV_WRITABLE;
    }
    return ::uv_poll_start(&watch->poll, events, &socket_watch::on_events) == 0 ? 0 : -1;
}

int manager::on_socket(CURL*, curl_socket_t socket, int what, void* self, void* watch) noexcept {
    return static_cast<manager*>(self)->watch_socket(socket,
                                                     what,
                                                     static_cast<socket_watch*>(watch));
}

int manager::on_timeout(CURLM*, long timeout_ms, void* self) noexcept {
    auto& timer = static_cast<manager*>(self)->timer->timer;
    if(timeout_ms < 0) {
        ::uv_timer_stop(&timer);
        return 0;
    }
    // Curl asks for 0 to be driven at once, but not from inside this call.
    ::uv_timer_start(&timer,
                     &timer_watch::on_fire,
                     static_cast<std::uint64_t>((std::max)(timeout_ms, 1L)),
                     0);
    return 0;
}

}  // namespace kota::http
