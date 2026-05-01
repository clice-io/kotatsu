#pragma once

#include <atomic>
#include <memory>
#include <utility>

#include "kota/async/runtime/sync.h"
#include "kota/async/runtime/task.h"
#include "kota/async/runtime/when.h"

namespace kota {

class cancellation_token {
public:
    class state {
    public:
        bool is_cancelled() const noexcept {
            return cancelled.load(std::memory_order_acquire);
        }

        void cancel() noexcept {
            bool expected = false;
            if(!cancelled.compare_exchange_strong(expected,
                                                  true,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {
                return;
            }
            event.set();
        }

        /// Returns a task that never succeeds.
        ///
        /// This awaitable turns the sticky cancellation state stored in
        /// `cancel_event` back into task cancellation semantics.
        ///
        /// There are only two externally visible behaviors:
        /// 1. If the token is already cancelled, this task cancels immediately.
        /// 2. Otherwise it waits until the token's internal event is set, then
        ///    cancels itself immediately.
        ///
        /// The event itself only wakes the waiter; it does not propagate
        /// cancellation upward. The trailing `co_await cancel();` is therefore
        /// essential: it converts "the cancellation event has fired" into the
        /// coroutine state `Cancelled`, which is what with_token(...) and other
        /// callers rely on.
        task<> wait() {
            if(cancelled.load(std::memory_order_acquire)) {
                // Preserve cancellation semantics for already-fired tokens.
                co_await kota::cancel();
            }

            // Wait for the sticky cancellation marker to become observable.
            co_await event.wait();

            // Being woken by cancel_event means "cancellation happened"; convert
            // that wakeup into actual task cancellation.
            co_await kota::cancel();
        }

    private:
        class event event;
        std::atomic<bool> cancelled{false};
    };

    cancellation_token() noexcept = delete;
    cancellation_token(const cancellation_token&) noexcept = default;
    cancellation_token& operator=(const cancellation_token&) noexcept = default;

    bool cancelled() const noexcept {
        return state->is_cancelled();
    }

    task<> wait() const noexcept {
        return state->wait();
    }

private:
    friend class cancellation_source;

    explicit cancellation_token(std::shared_ptr<state> state) : state(std::move(state)) {}

    std::shared_ptr<state> state;
};

class cancellation_source {
public:
    cancellation_source() : state(std::make_shared<class cancellation_token::state>()) {}

    cancellation_source(const cancellation_source&) = delete;
    cancellation_source& operator=(const cancellation_source&) = delete;

    ~cancellation_source() {
        cancel();
    }

    void cancel() noexcept {
        state->cancel();
    }

    bool cancelled() const noexcept {
        return state->is_cancelled();
    }

    cancellation_token token() const noexcept {
        return cancellation_token(state);
    }

private:
    std::shared_ptr<class cancellation_token::state> state;
};

/// with_token: cancel a task when any of the given tokens fire.
/// Races the inner task against token wait tasks using when_any;
/// if any token fires, when_any reports cancellation explicitly.
template <typename T, typename E, typename C, std::same_as<cancellation_token>... Tokens>
    requires (sizeof...(Tokens) > 0)
task<T, E, cancellation> with_token(task<T, E, C> inner_task, Tokens... tokens) {
    if((tokens.cancelled() || ...)) {
        co_await cancel();
    }

    // Race the wrapped task against all token waits.
    //
    // The token side is pure cancellation: `tokens.wait()` never yields a value, it only
    // suspends until cancellation interrupts the underlying event wait. Because the inner
    // task is wrapped with `catch_cancel()`, the aggregate also exposes cancellation
    // explicitly, so a token firing now shows up as `race_result.is_cancelled()`.
    //
    // The inner task is wrapped with `catch_cancel()`, so its cancellation is converted into
    // the aggregate cancellation channel instead of nesting inside the success payload.
    auto race_result = co_await when_any(std::move(inner_task).catch_cancel(), tokens.wait()...);

    if constexpr(!std::is_void_v<E>) {
        if(race_result.has_error()) {
            co_await fail(std::move(race_result).error());
        }
    }

    // Guard value access with has_value() rather than relying on
    // co_await cancel() making subsequent code unreachable — MSVC's
    // coroutine codegen can fall through past a symmetric-transfer
    // suspension, reaching the dereference on a cancelled outcome.
    if constexpr(!std::is_void_v<T>) {
        if(race_result.has_value()) {
            co_return std::move(std::get<0>(*race_result));
        }
    } else {
        if(race_result.has_value()) {
            co_return;
        }
    }

    co_await cancel();
}

}  // namespace kota
