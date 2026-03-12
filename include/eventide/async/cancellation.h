#pragma once

#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>

#include "sync.h"
#include "task.h"
#include "when.h"

namespace eventide {

class cancellation_token {
public:
    class state {
    public:
        bool is_cancelled() const noexcept {
            return cancelled;
        }

        void cancel() noexcept {
            if(cancelled) {
                return;
            }
            cancelled = true;
            cancel_event.interrupt();
        }

        event cancel_event;

    private:
        bool cancelled = false;
    };

    cancellation_token(const cancellation_token&) noexcept = default;
    cancellation_token& operator=(const cancellation_token&) noexcept = default;

    cancellation_token(cancellation_token&& other) noexcept : state_(other.state_) {}

    cancellation_token& operator=(cancellation_token&& other) noexcept {
        state_ = other.state_;
        return *this;
    }

    bool cancelled() const noexcept {
        return state_->is_cancelled();
    }

    /// Returns a task that never succeeds.
    ///
    /// There are only two behaviors:
    /// 1. If the token is already cancelled, this task cancels immediately.
    /// 2. Otherwise it waits until the token's internal event is interrupted,
    ///    then the underlying event wait is cancelled.
    task<> wait() const {
        if(state_->is_cancelled()) {
            // Preserve cancellation semantics for already-fired tokens.
            co_await cancel();
        }
        // `cancel_event.interrupt()` cancels this wait; it does not produce a value.
        co_await state_->cancel_event.wait();
    }

private:
    friend class cancellation_source;

    explicit cancellation_token(std::shared_ptr<state> state) : state_(std::move(state)) {}

    std::shared_ptr<state> state_;
};

class cancellation_source {
public:
    cancellation_source() : state_(std::make_shared<cancellation_token::state>()) {}

    cancellation_source(const cancellation_source&) = delete;
    cancellation_source& operator=(const cancellation_source&) = delete;

    cancellation_source(cancellation_source&&) noexcept = default;

    cancellation_source& operator=(cancellation_source&& other) noexcept {
        if(this == &other) {
            return *this;
        }

        cancel();
        state_ = std::move(other.state_);
        return *this;
    }

    ~cancellation_source() {
        cancel();
    }

    void cancel() noexcept {
        if(auto s = state_) {
            s->cancel();
        }
    }

    bool cancelled() const noexcept {
        return state_ && state_->is_cancelled();
    }

    cancellation_token token() const noexcept {
        return cancellation_token(state_);
    }

private:
    std::shared_ptr<cancellation_token::state> state_;
};

/// with_token: cancel a task when any of the given tokens fire.
/// Races the inner task against token wait tasks using when_any;
/// if any token fires, when_any propagates cancellation automatically.
template <typename T, typename E, typename C, std::same_as<cancellation_token>... Tokens>
    requires (sizeof...(Tokens) > 0)
task<T, E, cancellation> with_token(task<T, E, C> inner_task, Tokens... tokens) {
    if((tokens.cancelled() || ...)) {
        co_await cancel();
    }

    // Race the wrapped task against all token waits.
    //
    // The token side is pure cancellation: `tokens.wait()` never yields a value, it only
    // suspends until cancellation interrupts the underlying event wait. Because that
    // cancellation is not caught here, a token firing makes `when_any(...)` cancel
    // immediately, and execution never reaches the code below.
    //
    // The inner task is wrapped with `catch_cancel()`, so its cancellation is converted into
    // a normal value of type `outcome<T, E, cancellation>` instead of cancelling the race.
    // That means reaching the next line implies the winner was the first branch.
    auto variant_result = co_await when_any(std::move(inner_task).catch_cancel(), tokens.wait()...);
    auto& task_result = std::get<0>(variant_result);

    // Re-emit the inner task's cancellation as the cancellation of with_token(...).
    if(task_result.is_cancelled()) {
        co_await cancel();
    }

    if constexpr(!std::is_void_v<E>) {
        if(task_result.has_error()) {
            co_return outcome_error(std::move(task_result).error());
        }
    }

    if constexpr(std::is_void_v<T>) {
        co_return outcome_value();
    } else {
        co_return std::move(*task_result);
    }
}

}  // namespace eventide
