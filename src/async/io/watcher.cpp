#include "kotatsu/async/io/watcher.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <utility>

#ifndef _WIN32
#include <csignal>
#include <mutex>
#endif

#include "awaiter.h"

namespace kotatsu::async {

#ifndef _WIN32
namespace {

/// Held while a signal watcher starts or stops: leaving a signal ignored
/// must not undo a start() elsewhere in between. Never destroyed: a watcher
/// of static storage may go after it.
std::mutex& signal_actions() {
    static auto& mutex = *new std::mutex;
    return mutex;
}

/// Leaves `signum`, which a watcher has just stopped watching, as `then`
/// says, if no watcher watches it any more: libuv has set the default action
/// then, and keeps its handler while any of its signal handles, kotatsu's or
/// another's, still watches the signal.
void leave_signal(int signum, signal::leave then) {
    if(then == signal::leave::default_action) {
        return;
    }
    struct sigaction now = {};
    ::sigaction(signum, nullptr, &now);
    if(now.sa_handler != SIG_DFL) {
        return;
    }
    struct sigaction ignored = {};
    ignored.sa_handler = SIG_IGN;
    ::sigaction(signum, &ignored, nullptr);
}

}  // namespace
#endif

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

#ifndef _WIN32
    /// What a signal watcher leaves its signal as, if it stops last. Here
    /// `signal` names the handle.
    async::signal::leave then = async::signal::leave::default_action;

    /// Stops a signal watcher, leaving its signal as `then` says.
    void stop_signal() {
        // Watching no signal (0): stopped, or closed with its loop.
        const int watched = signal.signum;
        if(watched == 0) {
            return;
        }
        std::scoped_lock lock(signal_actions());
        ::uv_signal_stop(&signal);
        leave_signal(watched, then);
    }

    /// Stops a signal watcher first, as stop() does, then closes the handle.
    static void destroy(Self* self) noexcept {
        if(self->handle.type == UV_SIGNAL) {
            self->stop_signal();
        }
        owned_handle::destroy(self);
    }
#endif

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
#ifdef _WIN32
        case UV_SIGNAL: ::uv_signal_stop(&self->signal); break;
#else
        case UV_SIGNAL: self->stop_signal(); break;
#endif
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

error signal::start(int signum, [[maybe_unused]] leave then) {
    if(!self) {
        return error::invalid_argument;
    }

    // Stopped, it watches no signal (0).
    const int watched = self->signal.signum;
#ifndef _WIN32
    std::scoped_lock lock(signal_actions());
#endif
    auto failed = error(::uv_signal_start(&self->signal, Self::on_signal, signum));
    // A switch, also one that fails, stops watching the old signal, whose
    // fires go.
    if(self->signal.signum != watched) {
        self->missed = 0;
#ifndef _WIN32
        if(watched != 0) {
            leave_signal(watched, self->then);
        }
#endif
    }
    if(failed) {
        return failed;
    }
#ifndef _WIN32
    self->then = then;
#endif
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

}  // namespace kotatsu::async
