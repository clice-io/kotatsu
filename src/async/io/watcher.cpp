#include "kota/async/io/watcher.h"

#include <algorithm>
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

    /// A new watcher's state, with the handle `init` sets up on `loop`; for
    /// the handles whose init cannot fail.
    template <typename Handle>
    static detail::unique_handle<Self> create(event_loop& loop, int (*init)(uv_loop_t*, Handle*)) {
        auto self = make();
        init(loop.native_handle(), reinterpret_cast<Handle*>(&self->handle));
        return self;
    }

    static void on_signal(uv_signal_t* handle, int) {
        on_fire(handle);
    }
};

watcher::watcher() noexcept = default;

watcher::watcher(detail::unique_handle<Self> self) noexcept : self(std::move(self)) {}

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
    self->slot.abort(*self->handle.loop);
    return {};
}

error watcher::start() {
    if(!self) {
        return error::invalid_argument;
    }

    switch(self->handle.type) {
        case UV_IDLE: return error(::uv_idle_start(&self->idle, Self::on_fire));
        case UV_PREPARE: return error(::uv_prepare_start(&self->prepare, Self::on_fire));
        case UV_CHECK: return error(::uv_check_start(&self->check, Self::on_fire));
        default: std::unreachable();
    }
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
    return timer(Self::create(loop, ::uv_timer_init));
}

error timer::start(std::chrono::milliseconds timeout, std::chrono::milliseconds repeat) {
    if(!self) {
        return error::invalid_argument;
    }

    assert(timeout.count() >= 0 && repeat.count() >= 0 && "timer times must not be negative");
    if(auto err = error(::uv_timer_start(&self->timer,
                                         Self::on_fire,
                                         static_cast<std::uint64_t>(timeout.count()),
                                         static_cast<std::uint64_t>(repeat.count())))) {
        return err;
    }
    self->missed = 0;
    return {};
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

    // Stopped, it watches no signal (0).
    const bool switching = self->signal.signum != signum;
    if(auto err = error(::uv_signal_start(&self->signal, Self::on_signal, signum))) {
        return err;
    }
    if(switching) {
        self->missed = 0;
    }
    return {};
}

idle idle::create(event_loop& loop) {
    return idle(Self::create(loop, ::uv_idle_init));
}

prepare prepare::create(event_loop& loop) {
    return prepare(Self::create(loop, ::uv_prepare_init));
}

check check::create(event_loop& loop) {
    return check(Self::create(loop, ::uv_check_init));
}

task<> sleep(std::chrono::milliseconds timeout, event_loop& loop) {
    auto t = timer::create(loop);
    t.start(timeout);
    // A fresh timer has no other waiter, and it lives until it fires: the
    // wait ends with no error.
    co_await t.self->slot.wait();
}

task<> detail::expire(std::chrono::milliseconds timeout, event_loop& loop) {
    // A deadline already past expires on the loop's next turn.
    co_await sleep(std::max(timeout, std::chrono::milliseconds::zero()), loop);
    co_await cancel();
}

}  // namespace kota
