#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "sync.h"
#include "task.h"
#include "when.h"

namespace eventide {

namespace detail {

class cancellation_state {
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

}  // namespace detail

class cancellation_token {
public:
    cancellation_token() = default;

    bool cancelled() const noexcept {
        return state && state->is_cancelled();
    }

    /// Returns a task that suspends until the token is cancelled.
    /// If already cancelled, self-cancels immediately.
    task<> wait() const {
        if(!state) {
            event never;
            co_await never.wait();
            co_return;
        }
        if(state->is_cancelled()) {
            co_await cancel();
        }
        co_await state->cancel_event.wait();
    }

private:
    friend class cancellation_source;

    explicit cancellation_token(std::shared_ptr<detail::cancellation_state> state) :
        state(std::move(state)) {}

    std::shared_ptr<detail::cancellation_state> state;
};

class cancellation_source {
public:
    cancellation_source() : state(std::make_shared<detail::cancellation_state>()) {}

    cancellation_source(const cancellation_source&) = delete;
    cancellation_source& operator=(const cancellation_source&) = delete;

    cancellation_source(cancellation_source&&) noexcept = default;

    cancellation_source& operator=(cancellation_source&& other) noexcept {
        if(this == &other) {
            return *this;
        }

        cancel();
        state = std::move(other.state);
        return *this;
    }

    ~cancellation_source() {
        cancel();
    }

    void cancel() noexcept {
        if(auto s = state) {
            s->cancel();
        }
    }

    bool cancelled() const noexcept {
        return state && state->is_cancelled();
    }

    cancellation_token token() const noexcept {
        return cancellation_token(state);
    }

private:
    std::shared_ptr<detail::cancellation_state> state;
};

namespace detail {

/// Wraps a task so its full outcome (value/error/cancellation) becomes
/// the plain value type, allowing when_any to forward it through result().
template <typename T, typename E, typename C>
task<outcome<T, E, cancellation>> into_outcome(task<T, E, C> inner) {
    co_return co_await std::move(inner).catch_cancel();
}

}  // namespace detail

/// with_token: cancel a task when any of the given tokens fire.
/// Races the inner task against token wait tasks using when_any;
/// if any token fires, when_any propagates cancellation automatically.
template <typename T, typename E, typename C, std::same_as<cancellation_token>... Tokens>
    requires (sizeof...(Tokens) > 0)
task<T, E, cancellation> with_token(task<T, E, C> inner_task, Tokens... tokens) {
    if((tokens.cancelled() || ...)) {
        co_await cancel();
    }

    // when_any races the wrapped task against all token waits.
    // If a token fires: event.interrupt() → token.wait() cancelled →
    // when_any propagates cancellation → with_token task cancelled directly.
    // If the task completes: when_any returns with the outcome at index 0.
    auto variant_result =
        co_await when_any(detail::into_outcome(std::move(inner_task)), tokens.wait()...);
    auto& task_result = std::get<0>(variant_result);

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
