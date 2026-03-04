#include "eventide/async/watcher.h"

#include <cassert>
#include <chrono>
#include <memory>

#include "awaiter.h"
#include "eventide/async/error.h"
#include "eventide/async/loop.h"

namespace eventide {

struct timer::Self : uv_handle<timer::Self, uv_timer_t> {
    uv_timer_t handle{};
    system_op* waiter = nullptr;
    int pending = 0;
};

struct idle::Self : uv_handle<idle::Self, uv_idle_t> {
    uv_idle_t handle{};
    system_op* waiter = nullptr;
    int pending = 0;
};

struct prepare::Self : uv_handle<prepare::Self, uv_prepare_t> {
    uv_prepare_t handle{};
    system_op* waiter = nullptr;
    int pending = 0;
};

struct check::Self : uv_handle<check::Self, uv_check_t> {
    uv_check_t handle{};
    system_op* waiter = nullptr;
    int pending = 0;
};

struct signal::Self : uv_handle<signal::Self, uv_signal_t> {
    uv_signal_t handle{};
    system_op* waiter = nullptr;
    error* active = nullptr;
    int pending = 0;
};

namespace {

template <typename SelfT, typename HandleT>
struct basic_tick_await : uv::await_op<basic_tick_await<SelfT, HandleT>> {
    using await_base = uv::await_op<basic_tick_await<SelfT, HandleT>>;
    using promise_t = task<>::promise_type;

    // Watcher self that owns waiter/pending counters.
    SelfT* self;

    explicit basic_tick_await(SelfT* watcher) : self(watcher) {}

    static void on_cancel(system_op* op) {
        await_base::complete_cancel(op, [](auto& aw) {
            if(aw.self) {
                aw.self->waiter = nullptr;
            }
        });
    }

    static void on_fire(HandleT* handle) {
        auto* watcher = static_cast<SelfT*>(handle->data);
        assert(watcher != nullptr && "on_fire requires watcher state in handle->data");

        if(watcher->waiter) {
            auto w = watcher->waiter;
            watcher->waiter = nullptr;
            w->complete();
        } else {
            watcher->pending += 1;
        }
    }

    bool await_ready() const noexcept {
        return self && self->pending > 0;
    }

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<promise_t> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        if(!self) {
            return waiting;
        }
        self->waiter = this;
        return this->link_continuation(&waiting.promise(), location);
    }

    void await_resume() noexcept {
        if(self && self->pending > 0) {
            self->pending -= 1;
        }

        if(self) {
            self->waiter = nullptr;
        }
    }
};

using timer_await = basic_tick_await<timer::Self, uv_timer_t>;
using idle_await = basic_tick_await<idle::Self, uv_idle_t>;
using prepare_await = basic_tick_await<prepare::Self, uv_prepare_t>;
using check_await = basic_tick_await<check::Self, uv_check_t>;

struct signal_await : uv::await_op<signal_await> {
    using await_base = uv::await_op<signal_await>;
    using promise_t = task<error>::promise_type;

    // Signal watcher self that owns waiter/active pointers.
    signal::Self* self;
    // Result slot returned by await_resume().
    error result{};

    explicit signal_await(signal::Self* watcher) : self(watcher) {}

    static void on_cancel(system_op* op) {
        await_base::complete_cancel(op, [](auto& aw) {
            if(aw.self) {
                aw.self->waiter = nullptr;
                aw.self->active = nullptr;
            }
        });
    }

    static void on_fire(uv_signal_t* handle) {
        auto* watcher = static_cast<signal::Self*>(handle->data);
        assert(watcher != nullptr && "on_fire requires watcher state in handle->data");

        if(watcher->waiter && watcher->active) {
            *watcher->active = {};

            auto w = watcher->waiter;
            watcher->waiter = nullptr;
            watcher->active = nullptr;

            w->complete();
        } else {
            watcher->pending += 1;
        }
    }

    bool await_ready() const noexcept {
        return self && self->pending > 0;
    }

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<promise_t> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        if(!self) {
            return waiting;
        }
        self->waiter = this;
        self->active = &result;
        return this->link_continuation(&waiting.promise(), location);
    }

    error await_resume() noexcept {
        if(self && self->pending > 0) {
            self->pending -= 1;
        }

        if(self) {
            self->waiter = nullptr;
            self->active = nullptr;
        }
        return result;
    }
};

}  // namespace

#define EVENTIDE_DEFINE_WATCHER_SPECIAL_MEMBERS(WatcherType)                                       \
    WatcherType::WatcherType() noexcept = default;                                                 \
    WatcherType::WatcherType(unique_handle<Self> self) noexcept : self(std::move(self)) {}         \
    WatcherType::~WatcherType() = default;                                                         \
    WatcherType::WatcherType(WatcherType&& other) noexcept = default;                              \
    WatcherType& WatcherType::operator=(WatcherType&& other) noexcept = default;                   \
    WatcherType::Self* WatcherType::operator->() noexcept {                                        \
        return self.get();                                                                         \
    }

EVENTIDE_DEFINE_WATCHER_SPECIAL_MEMBERS(timer)
EVENTIDE_DEFINE_WATCHER_SPECIAL_MEMBERS(signal)
EVENTIDE_DEFINE_WATCHER_SPECIAL_MEMBERS(idle)
EVENTIDE_DEFINE_WATCHER_SPECIAL_MEMBERS(prepare)
EVENTIDE_DEFINE_WATCHER_SPECIAL_MEMBERS(check)

#undef EVENTIDE_DEFINE_WATCHER_SPECIAL_MEMBERS

timer timer::create(event_loop& loop) {
    auto self = Self::make();
    auto& handle = self->handle;
    uv::timer_init(loop, handle);

    return timer(std::move(self));
}

void timer::start(std::chrono::milliseconds timeout, std::chrono::milliseconds repeat) {
    if(!self) {
        return;
    }

    auto& handle = self->handle;
    assert(timeout.count() >= 0 && "timer::start timeout must be non-negative");
    assert(repeat.count() >= 0 && "timer::start repeat must be non-negative");
    uv::timer_start(
        handle,
        [](uv_timer_t* h) { timer_await::on_fire(h); },
        static_cast<std::uint64_t>(timeout.count()),
        static_cast<std::uint64_t>(repeat.count()));
}

void timer::stop() {
    if(!self) {
        return;
    }

    uv::timer_stop(self->handle);
}

task<> timer::wait() {
    if(!self) {
        co_return;
    }

    if(self->pending > 0) {
        self->pending -= 1;
        co_return;
    }

    if(self->waiter != nullptr) {
        assert(false && "timer::wait supports a single waiter at a time");
        co_return;
    }

    co_await timer_await{self.get()};
}

result<signal> signal::create(event_loop& loop) {
    auto self = Self::make();
    auto& handle = self->handle;
    if(auto err = uv::signal_init(loop, handle)) {
        return std::unexpected(err);
    }

    return signal(std::move(self));
}

error signal::start(int signum) {
    if(!self) {
        return error::invalid_argument;
    }

    auto& handle = self->handle;
    if(auto err = uv::signal_start(
           handle,
           [](uv_signal_t* h, int) { signal_await::on_fire(h); },
           signum);
       err) {
        return err;
    }

    return {};
}

error signal::stop() {
    if(!self) {
        return error::invalid_argument;
    }

    if(auto err = uv::signal_stop(self->handle)) {
        return err;
    }

    return {};
}

task<error> signal::wait() {
    if(!self) {
        co_return error::invalid_argument;
    }

    if(self->pending > 0) {
        self->pending -= 1;
        co_return error{};
    }

    if(self->waiter != nullptr) {
        co_return error::connection_already_in_progress;
    }

    co_return co_await signal_await{self.get()};
}

#define EVENTIDE_DEFINE_TICK_WATCHER_METHODS(WatcherType,                                          \
                                             HandleType,                                           \
                                             AwaiterType,                                          \
                                             INIT_FN,                                              \
                                             START_FN,                                             \
                                             STOP_FN,                                              \
                                             NameLiteral)                                          \
    WatcherType WatcherType::create(event_loop& loop) {                                            \
        auto self = Self::make();                                                                  \
        auto& handle = self->handle;                                                               \
        INIT_FN(loop, handle);                                                                     \
                                                                                                   \
        return WatcherType(std::move(self));                                                       \
    }                                                                                              \
                                                                                                   \
    void WatcherType::start() {                                                                    \
        if(!self) {                                                                                \
            return;                                                                                \
        }                                                                                          \
                                                                                                   \
        auto& handle = self->handle;                                                               \
        START_FN(handle, [](HandleType* h) { AwaiterType::on_fire(h); });                          \
    }                                                                                              \
                                                                                                   \
    void WatcherType::stop() {                                                                     \
        if(!self) {                                                                                \
            return;                                                                                \
        }                                                                                          \
                                                                                                   \
        STOP_FN(self->handle);                                                                     \
    }                                                                                              \
                                                                                                   \
    task<> WatcherType::wait() {                                                                   \
        if(!self) {                                                                                \
            co_return;                                                                             \
        }                                                                                          \
                                                                                                   \
        if(self->pending > 0) {                                                                    \
            self->pending -= 1;                                                                    \
            co_return;                                                                             \
        }                                                                                          \
                                                                                                   \
        if(self->waiter != nullptr) {                                                              \
            assert(false && NameLiteral "::wait supports a single waiter at a time");              \
            co_return;                                                                             \
        }                                                                                          \
                                                                                                   \
        co_await AwaiterType{self.get()};                                                          \
    }

EVENTIDE_DEFINE_TICK_WATCHER_METHODS(idle,
                                     uv_idle_t,
                                     idle_await,
                                     uv::idle_init,
                                     uv::idle_start,
                                     uv::idle_stop,
                                     "idle")

EVENTIDE_DEFINE_TICK_WATCHER_METHODS(prepare,
                                     uv_prepare_t,
                                     prepare_await,
                                     uv::prepare_init,
                                     uv::prepare_start,
                                     uv::prepare_stop,
                                     "prepare")

EVENTIDE_DEFINE_TICK_WATCHER_METHODS(check,
                                     uv_check_t,
                                     check_await,
                                     uv::check_init,
                                     uv::check_start,
                                     uv::check_stop,
                                     "check")

#undef EVENTIDE_DEFINE_TICK_WATCHER_METHODS

task<> sleep(std::chrono::milliseconds timeout, event_loop& loop) {
    auto t = timer::create(loop);
    t.start(timeout, std::chrono::milliseconds{0});
    co_await t.wait();
}

}  // namespace eventide
