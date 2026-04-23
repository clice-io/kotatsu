#include <memory>
#include <source_location>

#include "../async/io/awaiter.h"
#include "kota/http/detail/inflight_request.h"
#include "kota/http/detail/manager.h"
#include "kota/http/detail/request.h"
#include "kota/http/detail/runtime.h"

namespace kota::http {

namespace detail {

struct request_awaiter;

struct inflight_request_state : std::enable_shared_from_this<inflight_request_state> {
    explicit inflight_request_state(http::request req) noexcept : request(std::move(req)) {}

    manager* mgr = nullptr;
    inflight_request request;
    request_awaiter* awaiter = nullptr;
    bool registered = false;
    bool completed = false;
    bool request_released = false;

    void detach_from_multi() noexcept {
        if(!registered || !mgr || request_released || !request.easy) {
            registered = false;
            return;
        }

        request.clear_runtime_binding();
        mgr->remove_request(request.easy.get());
        registered = false;
    }

    void release_request() noexcept {
        if(!request_released) {
            request.clear_runtime_binding();
            request.easy.reset();
            request_released = true;
        }
    }

    void complete(error err, bool resume) noexcept;

    void complete(curl::easy_error code, bool resume) noexcept {
        complete(error::from_curl(code), resume);
    }
};

struct request_awaiter : uv::await_op<request_awaiter> {
    using promise_t = task<response, error>::promise_type;
    using result_type = outcome<response, error, cancellation>;

    inflight_request_ref state;

    request_awaiter(manager& manager, inflight_request_ref request_state) :
        state(std::move(request_state)) {
        state->mgr = &manager;
        state->awaiter = this;
    }

    ~request_awaiter() {
        if(!state) {
            return;
        }

        state->detach_from_multi();
        state->release_request();
        state->awaiter = nullptr;
        state->mgr = nullptr;
    }

    static void on_cancel(system_op* op) {
        uv::await_op<request_awaiter>::complete_cancel(op, [](request_awaiter& self) {
            self.state->detach_from_multi();
            self.state->release_request();
            self.state->completed = true;
        });
    }

    void start() noexcept {
        if(state->completed || state->request_released || !state->request.easy) {
            return;
        }

        if(!state->request.bind_runtime(inflight_request_opaque(state))) {
            if(state->request.result.kind == error_kind::curl &&
               curl::ok(state->request.result.curl_code)) {
                state->request.result = error::invalid_request("request runtime binding failed");
            }
            state->completed = true;
            return;
        }

        if(auto err = state->mgr->add_request(state->request.easy.get()); !curl::ok(err)) {
            state->request.fail(error::from_curl(curl::to_easy_error(err)));
            state->completed = true;
            return;
        }

        state->registered = true;
        state->mgr->drive_timeout_arming(inflight_request_opaque(state));
    }

    bool await_ready() const noexcept {
        return state->completed;
    }

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<promise_t> waiting,
                      std::source_location loc = std::source_location::current()) noexcept {
        return this->link_continuation(&waiting.promise(), loc);
    }

    result_type await_resume() noexcept {
        state->detach_from_multi();

        if(static_cast<async_node&>(*this).state == async_node::Cancelled) {
            return result_type(outcome_cancel(cancellation("http request cancelled")));
        }

        if(state->request_released) {
            return result_type(outcome_error(error::invalid_request("request state already released")));
        }

        return state->request.finish();
    }
};

void inflight_request_state::complete(error err, bool resume) noexcept {
    if(completed) {
        return;
    }

    completed = true;
    registered = false;
    if(!request_released) {
        request.result = std::move(err);
    }

    auto* waiting = awaiter;
    if(resume && waiting) {
        waiting->complete();
    }
}

inflight_request_ref make_inflight_request_state(http::request request) noexcept {
    return std::make_shared<inflight_request_state>(std::move(request));
}

void* inflight_request_opaque(const inflight_request_ref& request) noexcept {
    return request.get();
}

inflight_request_ref retain_inflight_request(void* opaque) noexcept {
    auto* request = static_cast<inflight_request_state*>(opaque);
    if(!request) {
        return {};
    }

    return request->weak_from_this().lock();
}

void mark_inflight_request_removed(const inflight_request_ref& request) noexcept {
    if(request) {
        request->registered = false;
    }
}

void complete_inflight_request(const inflight_request_ref& request,
                               curl::easy_error result,
                               bool resume_inline) noexcept {
    if(!request) {
        return;
    }

    request->complete(result, !resume_inline);
}

task<response, error> execute_request(http::request request, event_loop& loop) {
    // libcurl callbacks keep `userdata = this`, so the prepared request must stay at a stable
    // address for the rest of its lifetime.
    auto state = make_inflight_request_state(std::move(request));
    if(!state->request.prepare()) {
        co_await fail(std::move(state->request.result));
    }

    auto manager = manager::try_for_loop(loop);
    if(!manager) {
        co_await fail(std::move(manager.error()));
    }

    request_awaiter awaiter(manager->get(), std::move(state));
    awaiter.start();
    auto result = co_await awaiter;

    if(result.is_cancelled()) {
        co_await cancel();
    }

    if(result.has_error()) {
        co_await fail(std::move(result).error());
    }

    co_return std::move(*result);
}

}  // namespace detail

}  // namespace kota::http
