#pragma once

#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "frame.h"
#include "task.h"

namespace eventide {

namespace detail {

/// Extracts the async_node pointer from a task (for aggregate bookkeeping).
template <typename Task>
async_node* node_from(Task& task) {
    return task.operator->();
}

/// Moves the result out of a task (used by when_all::collect).
template <typename Task>
auto take_result(Task& task) {
    return task.result();
}

template <typename Task>
void release_inflight(Task& task) noexcept {
    auto* node = static_cast<standard_task*>(node_from(task));
    if(node && node->has_awaitee()) {
        node->detach_as_root();
        task.release();
    }
}

inline void destroy_or_detach(async_node* child) noexcept {
    assert(child && child->kind == async_node::NodeKind::Task);
    auto* task = static_cast<standard_task*>(child);

    if(task->has_awaitee()) {
        task->detach_as_root();
        return;
    }

    task->handle().destroy();
}

template <typename Tuple>
void release_inflight_all(Tuple& tasks) noexcept {
    std::apply([](auto&... ts) { (release_inflight(ts), ...); }, tasks);
}

template <typename Tuple>
void add_awaitees_all(std::vector<async_node*>& awaitees, Tuple& tasks) {
    std::apply([&](auto&... ts) { (awaitees.push_back(node_from(ts)), ...); }, tasks);
}

}  // namespace detail

/// Awaits all tasks concurrently. Returns a std::tuple of their results.
/// If any child cancels (without InterceptCancel), all siblings are cancelled
/// and cancellation propagates to the awaiting task.
template <typename... Tasks>
class when_all : public aggregate_op {
public:
    template <typename... U>
    explicit when_all(U&&... tasks) :
        aggregate_op(async_node::NodeKind::WhenAll), tasks_(std::forward<U>(tasks)...) {}

    ~when_all() {
        detail::release_inflight_all(tasks_);
    }

    bool await_ready() const noexcept {
        return sizeof...(Tasks) == 0;
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> awaiter_handle,
                      std::source_location location = std::source_location::current()) noexcept {
        total = sizeof...(Tasks);
        awaitees.clear();
        awaitees.reserve(total);
        detail::add_awaitees_all(awaitees, tasks_);
        return arm_and_resume(awaiter_handle, location, [this] { return pending_cancel; });
    }

    auto await_resume() {
        return collect(std::index_sequence_for<Tasks...>{});
    }

private:
    template <std::size_t... I>
    auto collect(std::index_sequence<I...>) {
        return std::tuple(detail::take_result(std::get<I>(tasks_))...);
    }

    std::tuple<Tasks...> tasks_;
};

/// Awaits the first task to complete. Returns the 0-based index of the winner.
/// All other tasks are cancelled. Does not collect results — access the
/// individual task objects to retrieve them.
template <typename... Tasks>
class when_any : public aggregate_op {
public:
    template <typename... U>
    explicit when_any(U&&... tasks) :
        aggregate_op(async_node::NodeKind::WhenAny), tasks_(std::forward<U>(tasks)...) {}

    ~when_any() {
        detail::release_inflight_all(tasks_);
    }

    bool await_ready() const noexcept {
        return sizeof...(Tasks) == 0;
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> awaiter_handle,
                      std::source_location location = std::source_location::current()) noexcept {
        total = sizeof...(Tasks);
        awaitees.clear();
        awaitees.reserve(total);
        detail::add_awaitees_all(awaitees, tasks_);
        return arm_and_resume(awaiter_handle, location, [this] {
            return done || pending_resume || pending_cancel;
        });
    }

    std::size_t await_resume() const noexcept {
        return winner;
    }

private:
    std::tuple<Tasks...> tasks_;
};

template <typename... Tasks>
when_all(Tasks&&...) -> when_all<std::decay_t<Tasks>...>;

template <typename... Tasks>
when_any(Tasks&&...) -> when_any<std::decay_t<Tasks>...>;

/// Dynamic structured concurrency: spawn N tasks at runtime, then
/// co_await the scope to wait for all of them. Unlike when_all (compile-time
/// variadic), scope uses a dynamic vector and takes ownership of spawned
/// tasks via task::release().
///
/// The scope destructor destroys children that have not started (or have
/// already quiesced), so it is safe to let the scope go out of scope without
/// awaiting. However, spawned tasks will NOT have been executed in that case.
///
/// Children still suspended on an in-flight awaitable are detached as root
/// tasks instead of being destroyed eagerly. This lets cooperative cancellation
/// finish and allows the child to destroy its own coroutine frame at
/// final_suspend after any outstanding callbacks have retired.
class async_scope : public aggregate_op {
public:
    async_scope() : aggregate_op(async_node::NodeKind::Scope) {}

    async_scope(const async_scope&) = delete;
    async_scope& operator=(const async_scope&) = delete;

    ~async_scope() {
        for(auto* child: awaitees) {
            if(child) {
                detail::destroy_or_detach(child);
            }
        }
    }

    template <typename T>
    void spawn(task<T>&& t) {
        auto* node = detail::node_from(t);
        t.release();
        awaitees.push_back(node);
        total += 1;
    }

    bool await_ready() const noexcept {
        return total == 0;
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> awaiter_handle,
                      std::source_location location = std::source_location::current()) noexcept {
        return arm_and_resume(awaiter_handle, location, [this] { return pending_cancel; });
    }

    void await_resume() noexcept {}
};

class event_loop;

task<> sleep(std::size_t milliseconds, event_loop& loop);

/// Runs a task with a timeout. If the task does not complete within the
/// specified duration, it is cancelled and cancellation propagates upward.
///
/// Returns cancel_result_t<T>: the task's value on success, or
/// unexpected(cancellation{}) on timeout or self-cancellation.
template <typename T>
task<std::conditional_t<is_cancellation_t<T>, T, std::expected<T, cancellation>>>
    with_timeout(task<T> t, std::size_t milliseconds, event_loop& loop) {
    using R = std::conditional_t<is_cancellation_t<T>, T, std::expected<T, cancellation>>;
    std::optional<R> result;

    auto wrapped = [&]() -> task<> {
        if constexpr(is_cancellation_t<T>) {
            result.emplace(co_await std::move(t));
        } else {
            result.emplace(co_await std::move(t).catch_cancel());
        }
    };

    auto timeout = [&]() -> task<> {
        co_await sleep(milliseconds, loop);
    };

    co_await when_any(wrapped(), timeout());

    if(result.has_value() && result->has_value()) {
        if constexpr(std::is_void_v<typename R::value_type>) {
            co_return;
        } else {
            co_return std::move(**result);
        }
    }

    co_await cancel();
    std::abort();
}

}  // namespace eventide
