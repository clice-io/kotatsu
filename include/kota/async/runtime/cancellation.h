#pragma once

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

#include "kota/support/functional.h"
#include "kota/async/runtime/task.h"
#include "kota/async/runtime/when.h"

namespace kota {

namespace detail {

/// What a cancellation_source shares with its tokens.
struct cancellation_state;

/// A callback cancellation_token::on_cancel() registered.
struct cancellation_node;

}  // namespace detail

/// A callback registered with cancellation_token::on_cancel(). Destroying the
/// registration, or assigning another to it, deregisters the callback if it
/// has not started yet. A default-constructed or moved-from registration
/// holds none.
class cancellation_callback {
public:
    cancellation_callback() noexcept;

    cancellation_callback(cancellation_callback&& other) noexcept;
    cancellation_callback& operator=(cancellation_callback&& other) noexcept;

    ~cancellation_callback();

private:
    friend class cancellation_token;

    explicit cancellation_callback(std::unique_ptr<detail::cancellation_node> node) noexcept;

    std::unique_ptr<detail::cancellation_node> node;
};

/// A view of a cancellation_source: whether it has cancelled, a wait for it
/// to, and callbacks it runs when it does. Copies share the source's state
/// and outlive the source.
class cancellation_token {
public:
    bool cancelled() const noexcept;

    /// Waits until the source cancels, then ends cancelled: it never succeeds.
    task<> wait() const;

    /// Registers `callback` to run once the source cancels: inside its
    /// cancel(), or its destructor, after the callbacks registered before it.
    /// Once the source has cancelled, `callback` runs at once instead, before
    /// on_cancel() returns, and the registration holds nothing. It runs once
    /// at most, and not at all if its registration goes first. It runs on the
    /// thread that cancels, the loop's, and must not throw, since cancel()
    /// cannot. It may destroy its own registration or others, which keeps
    /// those from running, and register more, which run at once.
    [[nodiscard]] cancellation_callback on_cancel(function<void()> callback) const;

private:
    friend class cancellation_source;

    explicit cancellation_token(std::shared_ptr<detail::cancellation_state> state) noexcept;

    std::shared_ptr<detail::cancellation_state> state;
};

/// Cancels the tasks its tokens guard, once: on cancel() or when it goes.
/// Like the tasks, it belongs to their loop's thread; cancel from another
/// thread by posting cancel() through a relay.
class cancellation_source {
public:
    cancellation_source();

    cancellation_source(const cancellation_source&) = delete;
    cancellation_source& operator=(const cancellation_source&) = delete;

    ~cancellation_source() {
        cancel();
    }

    /// Cancels: runs the callbacks of the tokens before it returns, and wakes
    /// their waits, which resume once whatever runs has suspended. Later calls
    /// do nothing. A callback may destroy the source.
    void cancel() noexcept;

    bool cancelled() const noexcept;

    cancellation_token token() const noexcept;

private:
    std::shared_ptr<detail::cancellation_state> state;
};

/// Runs `inner_task`, cancelling it once any of `tokens` fires: the cancel
/// reaches the task once whatever runs when the token fires has suspended,
/// not inside the source's cancel(). The result reports that cancellation,
/// or one of the task itself, as a value.
template <typename T, typename E, typename C, std::same_as<cancellation_token>... Tokens>
    requires (sizeof...(Tokens) > 0)
task<T, E, cancellation> with_token(task<T, E, C> inner_task, Tokens... tokens) {
    // A fired token keeps the task from starting at all.
    if((tokens.cancelled() || ...)) {
        co_await cancel();
    }

    // The token waits never succeed: they only end cancelled, which cancels
    // the race. The task's own cancellation, caught, does the same.
    auto race_result = co_await when_any(std::move(inner_task).catch_cancel(), tokens.wait()...);

    if constexpr(!std::is_void_v<E>) {
        if(race_result.has_error()) {
            co_await fail(std::move(race_result).error());
        }
    }
    if(race_result.is_cancelled()) {
        co_await cancel();
    }
    if constexpr(!std::is_void_v<T>) {
        co_return std::move(std::get<0>(*race_result));
    }
}

}  // namespace kota
