#pragma once

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstdlib>
#include <exception>
#include <optional>
#include <source_location>
#include <tuple>
#include <type_traits>
#include <utility>

#include "eventide/common/config.h"
#include "eventide/common/meta.h"
#include "eventide/async/io/loop.h"
#include "eventide/async/runtime/frame.h"
#include "eventide/async/vocab/awaitable.h"
#include "eventide/async/vocab/error.h"
#include "eventide/async/vocab/outcome.h"

namespace eventide {

// ============================================================================
// promise_result — two specializations
// ============================================================================

/// General case: stores outcome<T, E, void>.
/// C is never stored in the promise — cancellation uses task state.
/// Layout depends only on T and E; cancellation uses task state.
template <typename T, typename E, typename C>
struct promise_result {
    std::optional<outcome<T, E, void>> value;

    bool has_error_result() const noexcept {
        if constexpr(std::is_void_v<E>) {
            return false;
        } else {
            return value.has_value() && value->has_error();
        }
    }

    template <typename U>
    void return_value(U&& val) noexcept {
        value.emplace(std::forward<U>(val));
    }
};

/// Void-value tasks complete successfully via `co_return;`.
/// Error results can still be written explicitly for compatibility.
template <typename E, typename C>
struct promise_result<void, E, C> {
    std::optional<outcome<void, E, void>> value;

    bool has_error_result() const noexcept {
        if constexpr(std::is_void_v<E>) {
            return false;
        } else {
            return value.has_value() && value->has_error();
        }
    }

    void return_void() noexcept {
        value.emplace();
    }
};

// ============================================================================
// promise_exception, transition_await, cancel()
// ============================================================================

struct promise_exception {
#if ETD_ENABLE_EXCEPTIONS
    bool has_exception() const noexcept {
        return exception != nullptr;
    }

    std::exception_ptr get_exception() const noexcept {
        return exception;
    }

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
    bool has_exception() const noexcept {
        return false;
    }

    std::exception_ptr get_exception() const noexcept {
        return nullptr;
    }

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
            if(promise.has_exception()) {
                promise.state = async_node::Failed;
                promise.propagated_exception = promise.get_exception();
            } else if(promise.has_error_result()) {
                promise.state = async_node::Failed;
            } else {
                promise.state = state;
            }
        } else if(state == async_node::Cancelled) {
            promise.state = state;
        } else {
            assert(false && "unexpected task state");
        }
        return promise.final_transition();
    }

    [[noreturn]] void await_resume() const noexcept {
        std::abort();
    }
};

inline auto cancel() {
    return transition_await(async_node::Cancelled);
}

template <typename Outcome>
struct or_fail_await {
    Outcome result;
};

template <typename Outcome>
concept or_fail_result = is_outcome_v<std::remove_cvref_t<Outcome>> &&
                         std::is_void_v<typename std::remove_cvref_t<Outcome>::cancel_type> &&
                         (!std::is_void_v<typename std::remove_cvref_t<Outcome>::error_type>);

// Propagate the error channel of an outcome-like object; otherwise resume with its value.
template <typename Outcome>
    requires or_fail_result<Outcome>
auto or_fail(Outcome&& result) {
    return or_fail_await<std::remove_cvref_t<Outcome>>{std::forward<Outcome>(result)};
}

template <typename... Args>
struct fail_await {
    std::tuple<Args&&...> args;
};

template <typename... Args>
auto fail(Args&&... args) {
    return fail_await<Args...>{std::forward_as_tuple(std::forward<Args>(args)...)};
}

template <typename Outcome>
struct or_fail_resume_await {
    Outcome result;
    bool finish = false;

    bool await_ready() const noexcept {
        return !finish;
    }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
        return transition_await(async_node::Finished).await_suspend(handle);
    }

    decltype(auto) await_resume() noexcept {
        if constexpr(!std::is_void_v<typename Outcome::value_type>) {
            return std::move(*result);
        }
    }
};

// ============================================================================
// task<T, E, C>
// ============================================================================

template <typename T, typename E, typename C>
class task;

namespace detail {

template <typename Task>
struct or_fail_proxy {
    Task inner;
};

template <typename ParentPromise, typename ParentError, typename ChildTask>
std::coroutine_handle<> propagate_fail(async_node* child_node, async_node* parent_node) noexcept {
    auto* child = static_cast<typename ChildTask::promise_type*>(child_node);
    auto* parent = static_cast<ParentPromise*>(parent_node);
    auto* child_task = static_cast<standard_task*>(child_node);
    auto* parent_task = static_cast<standard_task*>(parent_node);

    child_task->clear_error_hook();

    if(child->propagated_exception) {
        return parent_task->handle();
    }

    assert(child->value.has_value() && child->value->has_error());
    parent->value.emplace(outcome_error(ParentError(std::move(*child->value).error())));
    parent_task->state = async_node::Failed;
    return parent_task->final_transition();
}

template <typename ParentPromise, typename ParentError, typename ChildTask>
struct or_fail_task_await {
    typename ChildTask::awaiter inner;

    bool await_ready() noexcept {
        return inner.await_ready();
    }

    auto await_suspend(std::coroutine_handle<ParentPromise> awaiter,
                       std::source_location location = std::source_location::current()) noexcept {
        inner.awaitee.h.promise().set_error_hook(
            &propagate_fail<ParentPromise, ParentError, ChildTask>);
        return inner.await_suspend(awaiter, location);
    }

    auto await_resume() {
        inner.awaitee.h.promise().clear_error_hook();
        if constexpr(!std::is_void_v<typename ChildTask::value_type>) {
            auto result = inner.await_resume();
            return typename ChildTask::value_type(std::move(*result));
        } else {
            inner.await_resume();
        }
    }
};

}  // namespace detail

template <typename T, typename E>
struct task_return_object;

template <typename T, typename E>
struct task_promise_object : standard_task, promise_result<T, E, void>, promise_exception {
    using coroutine_handle = std::coroutine_handle<task_promise_object>;

    using promise_result<T, E, void>::value;

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
        return task_return_object<T, E>{handle()};
    }

    template <typename... Args>
    auto await_transform(fail_await<Args...>&& fail) noexcept
        requires (!std::is_void_v<E>) && std::constructible_from<E, Args...> {
        value.emplace(outcome_error(std::apply(
            [](auto&&... forwarded) { return E(std::forward<decltype(forwarded)>(forwarded)...); },
            std::move(fail.args))));
        return transition_await(async_node::Finished);
    }

    template <typename Outcome>
    auto await_transform(or_fail_await<Outcome>&& failed) noexcept
        requires (!std::is_void_v<E>) &&
                 or_fail_result<Outcome> && std::constructible_from<E, typename Outcome::error_type>
    {
        if(failed.result.has_error()) {
            value.emplace(outcome_error(E(std::move(failed.result).error())));
            return or_fail_resume_await<Outcome>{std::move(failed.result), true};
        }
        return or_fail_resume_await<Outcome>{std::move(failed.result), false};
    }

    template <typename ChildT, typename ChildE>
    auto await_transform(detail::or_fail_proxy<task<ChildT, ChildE, void>>&& wrapped) noexcept
        requires (!std::is_void_v<E>) && std::constructible_from<E, ChildE> {
        using child_task = task<ChildT, ChildE, void>;
        return detail::or_fail_task_await<task_promise_object, E, child_task>{
            std::move(wrapped.inner).operator co_await()};
    }

    template <typename Awaitable>
    decltype(auto) await_transform(Awaitable&& awaitable) noexcept {
        return std::forward<Awaitable>(awaitable);
    }

    task_promise_object() {
        this->address = handle().address();
    }
};

template <typename T, typename E>
struct task_return_object {
    using promise_type = task_promise_object<T, E>;
    using coroutine_handle = std::coroutine_handle<promise_type>;

    coroutine_handle handle;

    operator task<T, E, void>() & noexcept;

    operator task<T, E, void>() && noexcept;

    operator task<T, E, cancellation>() & noexcept;

    operator task<T, E, cancellation>() && noexcept;
};

template <typename T, typename E, typename C>
class task {
public:
    friend class event_loop;
    template <typename, typename, typename>
    friend class task;
    template <typename, typename, typename>
    friend struct detail::or_fail_task_await;

    static_assert(std::is_void_v<C> || std::same_as<C, cancellation>,
                  "task only supports void or cancellation cancel channels");

    using value_type = T;
    using error_type = E;
    using cancel_type = C;

    using promise_type = task_promise_object<T, E>;

    using coroutine_handle = std::coroutine_handle<promise_type>;

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
                        return R(outcome_cancel(C{}));
                    } else {
                        std::abort();
                    }
                }

                if constexpr(std::is_void_v<E>) {
                    assert(promise.state == async_node::Finished);
                } else {
                    assert(promise.state == async_node::Finished ||
                           promise.state == async_node::Failed);
                }
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

    auto or_fail() && noexcept
        requires (!std::is_void_v<E>) && std::is_void_v<C> {
        return detail::or_fail_proxy<task>{std::move(*this)};
    }

public:
    task() = default;

    explicit task(coroutine_handle h) noexcept : h(h) {
        if constexpr(!std::is_void_v<C>) {
            this->h.promise().intercept_cancel();
        }
    }

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
        if constexpr(std::is_void_v<E> && std::is_void_v<C>) {
            if constexpr(!std::is_void_v<T>) {
                assert(promise.value.has_value() && "result() on empty return");
                return std::move(**promise.value);
            } else {
                return std::nullopt;
            }
        } else if constexpr(std::is_void_v<C>) {
            assert(promise.value.has_value() && "result() on empty return");
            return std::move(*promise.value);
        } else {
            return take_outcome(promise);
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

    /// Adds cancellation interception. Idempotent if already intercepting.
    auto catch_cancel() && {
        if constexpr(std::same_as<C, cancellation>) {
            return std::move(*this);
        } else {
            h.promise().intercept_cancel();
            auto handle = h;
            h = nullptr;
            using target = task<T, E, cancellation>;
            using target_handle = typename target::coroutine_handle;
            return target(target_handle::from_address(handle.address()));
        }
    }

private:
    static auto take_outcome(promise_type& promise) {
        using R = outcome<T, E, cancellation>;

        if(promise.state == async_node::Cancelled) {
            return R(outcome_cancel(cancellation{}));
        }

        if constexpr(std::is_void_v<E>) {
            assert(promise.state == async_node::Finished);
        } else {
            assert(promise.state == async_node::Finished || promise.state == async_node::Failed);
        }
        assert(promise.value.has_value() && "result() on empty return");

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

    coroutine_handle h;
};

template <typename T, typename E>
task_return_object<T, E>::operator task<T, E, void>() & noexcept {
    auto out = task<T, E, void>(handle);
    handle = nullptr;
    return out;
}

template <typename T, typename E>
task_return_object<T, E>::operator task<T, E, void>() && noexcept {
    auto out = task<T, E, void>(handle);
    handle = nullptr;
    return out;
}

template <typename T, typename E>
task_return_object<T, E>::operator task<T, E, cancellation>() & noexcept {
    handle.promise().intercept_cancel();
    auto out = task<T, E, cancellation>(handle);
    handle = nullptr;
    return out;
}

template <typename T, typename E>
task_return_object<T, E>::operator task<T, E, cancellation>() && noexcept {
    handle.promise().intercept_cancel();
    auto out = task<T, E, cancellation>(handle);
    handle = nullptr;
    return out;
}

namespace detail {

template <typename T>
constexpr inline bool is_task_v = is_specialization_of<task, std::remove_cvref_t<T>>;

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
    requires (!is_task_v<Awaitable>) && (!std::is_reference_v<Awaitable>) &&
             std::constructible_from<std::remove_cvref_t<Awaitable>, Awaitable&&> &&
             awaitable<std::remove_cvref_t<Awaitable>&&>
auto normalize_task_impl(std::remove_cvref_t<Awaitable> value)
    -> task<normalized_await_result_t<Awaitable>> {
    if constexpr(!std::is_void_v<normalized_await_result_t<Awaitable>>) {
        co_return co_await std::move(value);
    } else {
        co_await std::move(value);
    }
}

template <typename Awaitable>
    requires (!is_task_v<Awaitable>) && (!std::is_reference_v<Awaitable>) &&
             std::constructible_from<std::remove_cvref_t<Awaitable>, Awaitable&&> &&
             awaitable<std::remove_cvref_t<Awaitable>&&>
auto normalize_task(Awaitable&& input) -> task<normalized_await_result_t<Awaitable>> {
    return normalize_task_impl<Awaitable>(
        std::remove_cvref_t<Awaitable>(std::forward<Awaitable>(input)));
}

}  // namespace detail

}  // namespace eventide
