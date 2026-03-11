#pragma once

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstdlib>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

#include "awaitable.h"
#include "error.h"
#include "frame.h"
#include "loop.h"
#include "outcome.h"
#include "eventide/common/meta.h"

namespace eventide {

// ============================================================================
// promise_result — two specializations
// ============================================================================

/// General case: stores outcome<T, E, void>.
/// C is never stored in the promise — cancellation uses task state.
/// Layout depends only on T and E, enabling the from_address trick
/// in catch_cancel() (adding C does not change the promise layout).
template <typename T, typename E, typename C>
struct promise_result {
    std::optional<outcome<T, E, void>> value;

    template <typename U>
    void return_value(U&& val) noexcept {
        value.emplace(std::forward<U>(val));
    }
};

/// All-void: the only case that needs return_void().
/// Includes a `value` field so the layout matches the general template,
/// enabling the from_address trick in catch_cancel().
template <>
struct promise_result<void, void, void> {
    std::optional<outcome<void, void, void>> value;

    void return_void() noexcept {
        value.emplace();
    }
};

// ============================================================================
// promise_exception, transition_await, cancel(), fail()
// ============================================================================

struct promise_exception {
#ifdef __cpp_exceptions
    void unhandled_exception() noexcept {
        this->exception = std::current_exception();
    }

    void rethrow_if_exception() {
        if(this->exception) {
            std::rethrow_exception(this->exception);
        }
    }

protected:
    std::exception_ptr exception{nullptr};
#else
    void unhandled_exception() {
        std::abort();
    }

    void rethrow_if_exception() {}
#endif
};

struct transition_await {
    async_node::State state = async_node::Pending;

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
        auto& promise = handle.promise();
        if(state == async_node::Finished) {
            if(promise.state == async_node::Cancelled || promise.state == async_node::Failed) {
                return promise.final_transition();
            }
            assert(promise.state == async_node::Running && "only running task could finish");
            promise.state = state;
        } else if(state == async_node::Cancelled) {
            promise.state = state;
        } else if(state == async_node::Failed) {
            promise.state = state;
        } else {
            assert(false && "unexpected task state");
        }
        return promise.final_transition();
    }

    void await_resume() const noexcept {}
};

inline auto cancel() {
    return transition_await(async_node::Cancelled);
}

inline auto fail() {
    return transition_await(async_node::Failed);
}

// ============================================================================
// task<T, E, C>
// ============================================================================

template <typename T, typename E, typename C>
class task {
public:
    friend class event_loop;

    struct promise_type;

    using coroutine_handle = std::coroutine_handle<promise_type>;

    struct promise_type : standard_task, promise_result<T, E, C>, promise_exception {
        auto handle() {
            return coroutine_handle::from_promise(*this);
        }

        auto initial_suspend() const noexcept {
            return std::suspend_always();
        }

        auto final_suspend() const noexcept {
            return transition_await(async_node::Finished);
        }

        auto get_return_object() {
            return task(handle());
        }

        promise_type() {
            this->address = handle().address();
            if constexpr(!std::is_void_v<C>) {
                this->policy = static_cast<Policy>(this->policy | InterceptCancel);
            }
            if constexpr(!std::is_void_v<E>) {
                this->policy = static_cast<Policy>(this->policy | InterceptError);
            }
        }
    };

    struct awaiter {
        task awaitee;

        bool await_ready() noexcept {
            return false;
        }

        template <typename Promise>
        auto await_suspend(
            std::coroutine_handle<Promise> awaiter,
            std::source_location location = std::source_location::current()) noexcept {
            return awaitee.h.promise().link_continuation(&awaiter.promise(), location);
        }

        auto await_resume() {
            auto& promise = awaitee.h.promise();
            promise.rethrow_if_exception();

            if constexpr(std::is_void_v<E> && std::is_void_v<C>) {
                if(promise.state != async_node::Finished) {
                    std::abort();
                }
                if constexpr(!std::is_void_v<T>) {
                    assert(promise.value.has_value() && "await_resume: value not set");
                    return std::move(**promise.value);
                }
            } else {
                using R = outcome<T, E, C>;

                if(promise.state == async_node::Cancelled) {
                    if constexpr(!std::is_void_v<C>) {
                        if(promise.has_outcome() && promise.get_outcome()->is_cancelled()) {
                            return R(outcome_cancelled(
                                std::move(*promise.get_outcome()->template as<C>())));
                        }
                        return R(outcome_cancelled(C{}));
                    } else {
                        std::abort();
                    }
                }

                if(promise.state == async_node::Failed) {
                    if constexpr(!std::is_void_v<E>) {
                        if(promise.has_outcome() && promise.get_outcome()->has_error()) {
                            return R(
                                outcome_error(std::move(*promise.get_outcome()->template as<E>())));
                        }
                        return R(outcome_error(E{}));
                    } else {
                        std::abort();
                    }
                }

                assert(promise.state == async_node::Finished);
                assert(promise.value.has_value());

                if constexpr(!std::is_void_v<E>) {
                    if(promise.value->has_error()) {
                        return R(outcome_error(std::move(*promise.value).error()));
                    }
                }

                if constexpr(!std::is_void_v<T>) {
                    return R(std::move(**promise.value));
                } else {
                    return R();
                }
            }
        }
    };

    auto operator co_await() && noexcept {
        return awaiter(std::move(*this));
    }

public:
    task() = default;

    explicit task(coroutine_handle h) noexcept : h(h) {}

    task(const task&) = delete;

    task(task&& other) noexcept : h(other.h) {
        other.h = nullptr;
    }

    task& operator=(const task&) = delete;

    task& operator=(task&& other) noexcept {
        if(this != &other) {
            if(h) {
                h.destroy();
            }
            h = other.h;
            other.h = nullptr;
        }
        return *this;
    }

    ~task() {
        if(h) {
            h.destroy();
        }
    }

    auto result() {
        auto&& promise = h.promise();
        promise.rethrow_if_exception();
        if constexpr(std::is_void_v<E>) {
            if constexpr(!std::is_void_v<T>) {
                assert(promise.value.has_value() && "result() on empty return");
                return std::move(**promise.value);
            } else {
                return std::nullopt;
            }
        } else {
            assert(promise.value.has_value() && "result() on empty return");
            return std::move(*promise.value);
        }
    }

    auto value() {
        auto&& promise = h.promise();
        promise.rethrow_if_exception();
        if constexpr(std::is_void_v<E>) {
            if constexpr(!std::is_void_v<T>) {
                if(promise.value.has_value()) {
                    return std::optional<T>(std::move(**promise.value));
                }
                return std::optional<T>();
            } else {
                return std::nullopt;
            }
        } else {
            return std::move(promise.value);
        }
    }

    void release() {
        this->h = nullptr;
    }

    async_node* operator->() {
        return &h.promise();
    }

    /// Adds cancellation interception via from_address (safe because C never
    /// changes the promise layout). Defaults to the erased `cancellation` type.
    /// Idempotent when TargetC matches the existing cancel channel.
    template <typename TargetC = cancellation>
        requires (!std::is_void_v<TargetC>)
    auto catch_cancel() && {
        if constexpr(std::same_as<C, TargetC>) {
            return std::move(*this);
        } else {
            if constexpr(std::is_void_v<C>) {
                h.promise().policy = static_cast<async_node::Policy>(h.promise().policy |
                                                                     async_node::InterceptCancel);
            }
            auto handle = h;
            h = nullptr;
            using target = task<T, E, TargetC>;
            using target_handle = typename target::coroutine_handle;
            return target(target_handle::from_address(handle.address()));
        }
    }

    /// Adds error interception via a wrapper coroutine (error type changes
    /// the promise layout). Defaults to the erased `error` type.
    /// Idempotent when TargetE matches the existing error channel.
    template <typename TargetE = error>
        requires (!std::is_void_v<TargetE>)
    auto catch_error() && {
        if constexpr(std::same_as<E, TargetE>) {
            return std::move(*this);
        } else {
            static_assert(std::is_void_v<E>, "cannot convert between error types");
            static_assert(std::is_void_v<C>, "use catch_all() to add both channels");
            h.promise().policy =
                static_cast<async_node::Policy>(h.promise().policy | async_node::InterceptError);
            return catch_error_wrapper<TargetE>(std::move(*this));
        }
    }

    /// Adds both interceptions. Defaults to the erased types.
    /// Idempotent when both channels already match the targets.
    template <typename TargetE = error, typename TargetC = cancellation>
        requires (!std::is_void_v<TargetE>) && (!std::is_void_v<TargetC>)
    auto catch_all() && {
        if constexpr(std::same_as<E, TargetE> && std::same_as<C, TargetC>) {
            return std::move(*this);
        } else {
            static_assert(std::is_void_v<E> && std::is_void_v<C>,
                          "already has error or cancel channel");
            h.promise().policy = static_cast<async_node::Policy>(
                h.promise().policy | async_node::InterceptError | async_node::InterceptCancel);
            return catch_all_wrapper<TargetE, TargetC>(std::move(*this));
        }
    }

private:
    template <typename TargetE>
    static task<T, TargetE, void> catch_error_wrapper(task<T, void, void> inner) {
        if constexpr(std::is_void_v<T>) {
            co_await std::move(inner);
            co_return outcome_value();
        } else {
            co_return co_await std::move(inner);
        }
    }

    template <typename TargetE, typename TargetC>
    static task<T, TargetE, TargetC> catch_all_wrapper(task<T, void, void> inner) {
        if constexpr(std::is_void_v<T>) {
            co_await std::move(inner);
            co_return outcome_value();
        } else {
            co_return co_await std::move(inner);
        }
    }

    coroutine_handle h;
};

namespace detail {

template <typename T>
constexpr inline bool is_task_v = is_specialization_of<task, std::remove_cvref_t<T>>;

template <typename T>
concept owned_awaitable = !std::is_lvalue_reference_v<T> && awaitable<T&&>;

template <typename T>
using normalized_await_result_t = await_result_t<std::remove_cvref_t<T>&&>;

template <typename T, typename = void>
struct normalized_task;

template <typename T>
struct normalized_task<T, std::enable_if_t<is_task_v<T>>> {
    using type = std::remove_cvref_t<T>;
};

template <typename T>
struct normalized_task<T, std::enable_if_t<!is_task_v<T> && awaitable<std::remove_cvref_t<T>&&>>> {
    using type = task<normalized_await_result_t<T>>;
};

template <typename T>
using normalized_task_t = typename normalized_task<T>::type;

template <typename T, typename E, typename C>
task<T, E, C> normalize_task(task<T, E, C>&& t) {
    return std::move(t);
}

template <typename Awaitable>
    requires (!is_task_v<Awaitable>) && owned_awaitable<Awaitable>
auto normalize_task_impl(std::remove_cvref_t<Awaitable> value)
    -> task<normalized_await_result_t<Awaitable>> {
    if constexpr(std::is_void_v<normalized_await_result_t<Awaitable>>) {
        co_await std::move(value);
        co_return;
    } else {
        co_return co_await std::move(value);
    }
}

template <typename Awaitable>
    requires (!is_task_v<Awaitable>) && owned_awaitable<Awaitable>
auto normalize_task(Awaitable&& input) -> task<normalized_await_result_t<Awaitable>> {
    return normalize_task_impl<Awaitable>(
        std::remove_cvref_t<Awaitable>(std::forward<Awaitable>(input)));
}

}  // namespace detail

}  // namespace eventide
