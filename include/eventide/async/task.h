#pragma once

#include <cassert>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

#include "error.h"
#include "frame.h"
#include "loop.h"

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

    ctask<T> catch_cancel() {
        if constexpr(is_cancellation_t<T>) {
            static_assert(!is_cancellation_t<T>, "already explicit cancellation");
        }

        h.promise().policy = async_node::InterceptCancel;
        auto handle = h;
        h = nullptr;
        /// We make sure they have totally same layout, so do it here is fine even if
        /// sounds like undefined behavior.
        using coroutine_handle = ctask<T>::coroutine_handle;
        return ctask<T>(coroutine_handle::from_address(handle.address()));
    }

private:
    coroutine_handle h;
};

template <typename T>
class shared_task;

template <typename T>
class shared_future : public waiter_link {
public:
    shared_future(shared_resource* resource) : waiter_link(async_node::NodeKind::SharedFuture) {
        resource->inc_ref();
        resource->insert(this);
    }

    shared_future(const shared_future&) = delete;

    shared_future(shared_future&& other) : waiter_link(async_node::NodeKind::SharedFuture) {
        auto temp = other.resource;
        temp->remove(&other);
        temp->insert(this);
    }

    ~shared_future() {
        if(resource) {
            auto temp = resource;
            temp->remove(this);
            temp->dec_ref();
        }
    }

    using promise_type = typename shared_task<T>::promise_type;

    bool await_ready() const noexcept {
        return static_cast<promise_type*>(resource)->value.has_value();
    }

    template <typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> awaiter,
                       std::source_location location = std::source_location::current()) noexcept {
        return link_continuation(&awaiter.promise(), location);
    }

    std::expected<T, cancellation> await_resume() {
        if(resource->state == Finished) {
            assert(await_ready() && "resume without value");
            return *static_cast<promise_type*>(resource)->value;
        } else {
            return std::unexpected(cancellation());
        }
    }
};

template <typename T>
class shared_task {
public:
    friend class event_loop;

    struct promise_type;

    using coroutine_handle = std::coroutine_handle<promise_type>;

    struct promise_type : shared_resource, promise_result<T>, promise_exception {
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
            return shared_task<T>(handle());
        }

        promise_type() : shared_resource(async_node::NodeKind::SharedTask) {
            this->address = handle().address();
        }
    };

public:
    shared_task() = default;

    explicit shared_task(coroutine_handle h) noexcept : h(h) {
        h.promise().inc_ref();
    }

    ~shared_task() {
        h.promise().dec_ref();
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

    async_node* operator->() {
        return &h.promise();
    }

    shared_future<T> get() {
        return shared_future<T>(&h.promise());
    }

private:
    coroutine_handle h;
};

}  // namespace eventide
