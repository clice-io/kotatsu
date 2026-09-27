#include "kota/async/io/watcher.h"

#include <cassert>
#include <cstddef>
#include <utility>

#include "awaiter.h"

namespace kota {

struct watcher::Self : uv::owned_handle<Self> {
    union {
        uv_handle_t handle;
        uv_timer_t timer;
        uv_signal_t signal;
        uv_idle_t idle;
        uv_prepare_t prepare;
        uv_check_t check;
    };

    uv::waiter_slot<void> slot;

    /// Fires nobody waited for yet.
    std::size_t missed = 0;

    void fire() {
        if(slot.waiting()) {
            slot.deliver({});
        } else {
            missed = handle.type == UV_SIGNAL ? missed + 1 : 1;
        }
    }

    template <typename Handle>
    static void on_fire(Handle* handle) {
        static_cast<Self*>(handle->data)->fire();
    }

    static void on_signal(uv_signal_t* handle, int) {
        on_fire(handle);
    }
};

watcher::watcher() noexcept = default;

watcher::watcher(unique_handle<Self> self) noexcept : self(std::move(self)) {}

watcher::watcher(watcher&& other) noexcept = default;

watcher& watcher::operator=(watcher&& other) noexcept = default;

watcher::~watcher() = default;

error watcher::stop() {
    if(!self) {
        return error::invalid_argument;
    }

    switch(self->handle.type) {
        case UV_TIMER: ::uv_timer_stop(&self->timer); break;
        case UV_SIGNAL: ::uv_signal_stop(&self->signal); break;
        case UV_IDLE: ::uv_idle_stop(&self->idle); break;
        case UV_PREPARE: ::uv_prepare_stop(&self->prepare); break;
        case UV_CHECK: ::uv_check_stop(&self->check); break;
        default: std::unreachable();
    }
    return {};
}

task<void, error> watcher::wait() {
    if(!self) {
        co_await fail(error::invalid_argument);
    }

    if(self->missed > 0) {
        self->missed -= 1;
        co_return;
    }

    if(auto err = co_await self->slot.wait()) {
        co_await fail(err);
    }
}

timer timer::create(event_loop& loop) {
    auto self = Self::make();
    ::uv_timer_init(loop.native_handle(), &self->timer);
    return timer(std::move(self));
}

error timer::start(std::chrono::milliseconds timeout, std::chrono::milliseconds repeat) {
    if(!self) {
        return error::invalid_argument;
    }

    assert(timeout.count() >= 0 && repeat.count() >= 0 && "timer times must not be negative");
    return error(::uv_timer_start(&self->timer,
                                  Self::on_fire,
                                  static_cast<std::uint64_t>(timeout.count()),
                                  static_cast<std::uint64_t>(repeat.count())));
}

result<signal> signal::create(event_loop& loop) {
    auto self = Self::make();
    if(auto err = error(::uv_signal_init(loop.native_handle(), &self->signal))) {
        return outcome_error(err);
    }
    return signal(std::move(self));
}

error signal::start(int signum) {
    if(!self) {
        return error::invalid_argument;
    }
    return error(::uv_signal_start(&self->signal, Self::on_signal, signum));
}

idle idle::create(event_loop& loop) {
    auto self = Self::make();
    ::uv_idle_init(loop.native_handle(), &self->idle);
    return idle(std::move(self));
}

error idle::start() {
    if(!self) {
        return error::invalid_argument;
    }
    return error(::uv_idle_start(&self->idle, Self::on_fire));
}

prepare prepare::create(event_loop& loop) {
    auto self = Self::make();
    ::uv_prepare_init(loop.native_handle(), &self->prepare);
    return prepare(std::move(self));
}

error prepare::start() {
    if(!self) {
        return error::invalid_argument;
    }
    return error(::uv_prepare_start(&self->prepare, Self::on_fire));
}

check check::create(event_loop& loop) {
    auto self = Self::make();
    ::uv_check_init(loop.native_handle(), &self->check);
    return check(std::move(self));
}

error check::start() {
    if(!self) {
        return error::invalid_argument;
    }
    return error(::uv_check_start(&self->check, Self::on_fire));
}

task<> sleep(std::chrono::milliseconds timeout, event_loop& loop) {
    auto t = timer::create(loop);
    t.start(timeout);
    // A fresh timer has no other waiter, and it lives until it fires: the
    // wait ends with no error.
    co_await t.self->slot.wait();
}

}  // namespace kota
