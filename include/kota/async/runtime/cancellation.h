#pragma once

#include <concepts>
#include <memory>
#include <utility>

#include "kota/async/runtime/sync.h"
#include "kota/async/runtime/task.h"
#include "kota/async/runtime/when.h"

namespace kota {

/// A view of a cancellation_source: whether it has cancelled, and a wait for
/// it to. Copies share the source's state and outlive the source.
class cancellation_token {
public:
    bool cancelled() const noexcept {
        return fired->is_set();
    }

    /// Waits until the source cancels, then ends cancelled: it never succeeds.
    task<> wait() const {
        return wait_for(fired);
    }

private:
    friend class cancellation_source;

    explicit cancellation_token(std::shared_ptr<event> fired) noexcept : fired(std::move(fired)) {}

    static task<> wait_for(std::shared_ptr<event> fired) {
        co_await fired->wait();
        co_await cancel();
    }

    std::shared_ptr<event> fired;
};

/// Cancels the tasks its tokens guard, once: on cancel() or when it goes.
class cancellation_source {
public:
    cancellation_source() : fired(std::make_shared<event>()) {}

    cancellation_source(const cancellation_source&) = delete;
    cancellation_source& operator=(const cancellation_source&) = delete;

    ~cancellation_source() {
        cancel();
    }

    void cancel() noexcept {
        fired->set();
    }

    bool cancelled() const noexcept {
        return fired->is_set();
    }

    cancellation_token token() const noexcept {
        return cancellation_token(fired);
    }

private:
    std::shared_ptr<event> fired;
};

/// Runs `inner_task`, cancelling it once any of `tokens` fires. The result
/// reports that cancellation, or one of the task itself, as a value.
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
