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
#include "eventide/common/meta.h"

namespace eventide {

template <typename T>
struct promise_result {
    std::optional<T> value;

    template <typename U>
    void return_value(U&& val) noexcept {
        value.emplace(std::forward<U>(val));
    }
};

template <typename T>
struct promise_result<std::expected<T, cancellation>> {
    std::optional<T> value;

    template <typename U>
    void return_value(U&& val) noexcept {
        value.emplace(std::forward<U>(val));
    }

    void return_value(cancellation) {}
};

template <>
struct promise_result<std::expected<void, cancellation>> {
    void return_void() noexcept {}
};

template <>
struct promise_result<void> {
    void return_void() noexcept {}
};

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
            // If a task was cancelled while awaiting and then reaches final_suspend during
            // cooperative cleanup, keep the cancellation state instead of asserting.
            if(promise.state == async_node::Cancelled) {
                return promise.final_transition();
            }
            assert(promise.state == async_node::Running && "only running task could finish");
            promise.state = state;
        } else if(state == async_node::Cancelled) {
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

template <typename T = void>
class task {
public:
    friend class event_loop;

    struct promise_type;

    using coroutine_handle = std::coroutine_handle<promise_type>;

    struct promise_type : standard_task, promise_result<T>, promise_exception {
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
            return task<T>(handle());
        }

        promise_type() {
            this->address = handle().address();
            if constexpr(is_cancellation_t<T>) {
                this->policy = InterceptCancel;
            }
        }
    };

    struct awaiter {
        task<T> awaitee;

        bool await_ready() noexcept {
            return false;
        }

        template <typename Promise>
        auto await_suspend(
            std::coroutine_handle<Promise> awaiter,
            std::source_location location = std::source_location::current()) noexcept {
            return awaitee.h.promise().link_continuation(&awaiter.promise(), location);
        }

        T await_resume() {
            auto& promise = awaitee.h.promise();
            promise.rethrow_if_exception();
            if(promise.state == async_node::Cancelled) {
                if constexpr(is_cancellation_t<T>) {
                    return std::unexpected(cancellation());
                } else if constexpr(is_status_t<T>) {
                    return std::unexpected(status(cancellation{}));
                } else {
                    /// Implicitly spread cancellation, never call this.
                    std::abort();
                }
            }

            if(promise.state == async_node::Finished) {
                if constexpr(is_cancellation_t<T>) {
                    if constexpr(!std::is_void_v<typename T::value_type>) {
                        assert(promise.value.has_value() && "await_resume: value not set");
                        return std::move(*awaitee.h.promise().value);
                    } else {
                        return {};
                    }
                } else {
                    if constexpr(!std::is_void_v<T>) {
                        assert(promise.value.has_value() && "await_resume: value not set");
                        return std::move(*awaitee.h.promise().value);
                    } else {
                        return;
                    }
                }
            }

            std::abort();
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
        if constexpr(!std::is_void_v<T>) {
            assert(promise.value.has_value() && "on empty return");
            return std::move(*promise.value);
        } else {
            return std::nullopt;
        }
    }

    auto value() {
        auto&& promise = h.promise();
        promise.rethrow_if_exception();
        if constexpr(!std::is_void_v<T>) {
            return std::move(promise.value);
        } else {
            return std::nullopt;
        }
    }

    void release() {
        this->h = nullptr;
    }

    async_node* operator->() {
        return &h.promise();
    }

    /// Converts task<T> to ctask<T> (= task<expected<T, cancellation>>).
    /// Sets InterceptCancel policy so cancellation is returned as a value
    /// instead of propagating upward.
    ///
    /// Implementation note: task<T> and ctask<T> share identical coroutine
    /// frame layout (same standard_task base). We reinterpret the handle
    /// via from_address to avoid a full coroutine type conversion.
    ctask<T> catch_cancel() {
        if constexpr(is_cancellation_t<T>) {
            static_assert(!is_cancellation_t<T>, "already explicit cancellation");
        }

        h.promise().policy = async_node::InterceptCancel;
        auto handle = h;
        h = nullptr;
        using coroutine_handle = ctask<T>::coroutine_handle;
        return ctask<T>(coroutine_handle::from_address(handle.address()));
    }

private:
    coroutine_handle h;
};

namespace detail {

template <typename T>
constexpr inline bool is_task_v = is_specialization_of<task, std::remove_cvref_t<T>>;

template <typename T>
concept owned_awaitable = !std::is_lvalue_reference_v<T> && awaitable<T&&>;

template <typename T>
using normalized_await_result_t = await_result_t<std::remove_cvref_t<T>&&>;

template <typename T, bool = is_task_v<T>>
struct normalized_task;

template <typename T>
struct normalized_task<T, true> {
    using type = std::remove_cvref_t<T>;
};

template <typename T>
struct normalized_task<T, false> {
    using type = task<normalized_await_result_t<T>>;
};

template <typename T>
using normalized_task_t = typename normalized_task<T>::type;

template <typename T>
task<T> normalize_task(task<T>&& t) {
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
