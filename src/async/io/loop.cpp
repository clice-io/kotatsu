#include "eventide/async/io/loop.h"

#include <cassert>
#include <deque>
#include <mutex>
#include <vector>

#include "../libuv.h"
#include "eventide/async/runtime/frame.h"

namespace eventide {

struct event_loop::self {
    uv_loop_t loop = {};
    uv_idle_t idle = {};
    uv_async_t async = {};
    bool idle_running = false;
    std::deque<async_node*> tasks;

    /// Guarded by `post_mutex`. Written by any thread via post(),
    /// drained on the event loop thread in the uv_async_t callback.
    std::mutex post_mutex;
    std::vector<std::function<void()>> posted;
};

static thread_local event_loop* current_loop = nullptr;

event_loop& event_loop::current() {
    assert(current_loop && "event_loop::current() called outside a running loop");
    return *current_loop;
}

void each(uv_idle_t* idle) {
    auto self = static_cast<struct event_loop::self*>(idle->data);
    if(self->idle_running && self->tasks.empty()) {
        self->idle_running = false;
        uv::idle_stop(*idle);
        return;
    }

    /// Resume may create new tasks, we want to run them in the next iteration.
    auto all = std::move(self->tasks);
    for(auto& task: all) {
        task->resume();
    }
}

void event_loop::schedule(async_node& frame, std::source_location location) {
    assert(self && "schedule: no current event loop in this thread");

    if(frame.state == async_node::Pending) {
        frame.state = async_node::Running;
    } else if(frame.state == async_node::Finished || frame.state == async_node::Running) {
        std::abort();
    }

    frame.location = location;
    auto& self = *this;
    if(!self->idle_running && self->tasks.empty()) {
        self->idle_running = true;
        uv::idle_start(self->idle, each);
    }
    self->tasks.push_back(&frame);
}

void on_post(uv_async_t* handle) {
    auto* self = static_cast<struct event_loop::self*>(handle->data);
    std::vector<std::function<void()>> batch;
    {
        std::lock_guard lock(self->post_mutex);
        batch.swap(self->posted);
    }
    for(auto& fn: batch) {
        fn();
    }
}

void event_loop::post(std::function<void()> callback) {
    assert(self && "post: event loop has been destroyed");
    {
        std::lock_guard lock(self->post_mutex);
        self->posted.push_back(std::move(callback));
    }
    uv::async_send(self->async);
}

event_loop::event_loop() : self(new struct self()) {
    auto& loop = self->loop;
    if(auto err = uv::loop_init(loop)) {
        abort();
    }

    auto& idle = self->idle;
    uv::idle_init(loop, idle);
    idle.data = self.get();

    auto& async = self->async;
    uv::async_init(loop, async, on_post);
    async.data = self.get();
    // Unref so the async handle alone does not keep the loop alive.
    uv::unref(async);
}

event_loop::~event_loop() {
    constexpr static auto cleanup = +[](uv_handle_t* h, void* arg) {
        auto* self = static_cast<struct event_loop::self*>(arg);
        if(!uv::is_closing(*h)) {
            auto* idle = uv::as_handle(self->idle);
            auto* async = uv::as_handle(self->async);
            if(h == idle || h == async) {
                uv::close(*h, nullptr);
                return;
            }

            uv::close(*h, [](uv_handle_t* handle) { uv::loop_close_fallback::mark(handle); });
        }
    };

    auto& loop = self->loop;
    auto close_err = uv::loop_close(loop);
    if(close_err.value() == UV_EBUSY) {
        uv::walk(loop, cleanup, self.get());

        // Run event loop to trigger all close callbacks.
        while((close_err = uv::loop_close(loop)).value() == UV_EBUSY) {
            uv::run(loop, UV_RUN_ONCE);
        }
    }
}

event_loop::operator uv_loop_t&() noexcept {
    return self->loop;
}

event_loop::operator const uv_loop_t&() const noexcept {
    return self->loop;
}

int event_loop::run() {
    auto previous = current_loop;
    current_loop = this;
    const int result = uv::run(self->loop, UV_RUN_DEFAULT);
    current_loop = previous;
    return result;
}

void event_loop::stop() {
    uv::stop(self->loop);
}

}  // namespace eventide
