#include "eventide/async/loop.h"

#include <cassert>
#include <deque>

#include "libuv.h"
#include "eventide/async/frame.h"

namespace eventide {

struct event_loop::self {
    uv_loop_t loop = {};
    uv_idle_t idle = {};
    bool idle_running = false;
    std::deque<async_node*> tasks;
};

static thread_local event_loop* current_loop = nullptr;

event_loop& event_loop::current() {
    return *current_loop;
}

void each(uv_idle_t* idle) {
    auto self = static_cast<struct event_loop::self*>(idle->data);
    if(self->idle_running && self->tasks.empty()) {
        self->idle_running = false;
        uv::idle_stop(*idle);
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

event_loop::event_loop() : self(new struct self()) {
    auto& loop = self->loop;
    if(auto err = uv::loop_init(loop)) {
        abort();
    }

    auto& idle = self->idle;
    uv::idle_init(loop, idle);
    uv::idle_start(idle, each);
    idle.data = self.get();
}

event_loop::~event_loop() {
    constexpr static auto cleanup = +[](uv_handle_t* h, void*) {
        if(!uv::is_closing(*h)) {
            uv::close(*h, nullptr);
        }
    };

    auto& loop = self->loop;
    auto close_err = uv::loop_close(loop);
    if(close_err.value() == UV_EBUSY) {
        uv::walk(loop, cleanup, nullptr);

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
