#pragma once

#include <cassert>
#include <cstddef>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "frame.h"
#include "task.h"
#include "eventide/common/small_vector.h"

namespace eventide {

namespace detail {

template <typename Task>
struct range_tasks {
    using task_type = Task;
};

template <typename Task>
using task_result_t = decltype(std::declval<Task&>().result());

template <typename Async>
using normalized_result_t = task_result_t<normalized_task_t<Async>>;

template <typename Range>
using range_async_value_t = std::ranges::range_value_t<std::remove_cvref_t<Range>>;

template <typename Range>
using normalized_range_task_t = normalized_task_t<range_async_value_t<Range>>;

template <typename Range>
using normalized_range_result_t = task_result_t<normalized_range_task_t<Range>>;

template <typename Range>
concept owned_async_range =
    !std::is_lvalue_reference_v<Range> && !std::is_const_v<std::remove_reference_t<Range>> &&
    std::ranges::input_range<std::remove_cvref_t<Range>> &&
    owned_awaitable<range_async_value_t<Range>>;

/// Extracts the async_node pointer from a task (for aggregate bookkeeping).
template <typename Task>
async_node* node_from(Task& task) {
    return task.operator->();
}

/// Moves the result out of a task.
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

template <typename Tasks>
void release_inflight_range(Tasks& tasks) noexcept {
    for(auto& task: tasks) {
        release_inflight(task);
    }
}

template <typename Tuple>
void add_awaitees_all(std::vector<async_node*>& awaitees, Tuple& tasks) {
    std::apply([&](auto&... ts) { (awaitees.push_back(node_from(ts)), ...); }, tasks);
}

template <typename Tasks>
void add_awaitees_range(std::vector<async_node*>& awaitees, Tasks& tasks) {
    for(auto& task: tasks) {
        awaitees.push_back(node_from(task));
    }
}

template <owned_async_range Range>
auto normalize_task_range(Range&& range) -> small_vector<normalized_range_task_t<Range>> {
    small_vector<normalized_range_task_t<Range>> tasks;
    if constexpr(std::ranges::sized_range<std::remove_reference_t<Range>>) {
        tasks.reserve(std::ranges::size(range));
    }

    for(auto&& async: range) {
        tasks.emplace_back(normalize_task(std::move(async)));
    }

    return tasks;
}

}  // namespace detail

/// Awaits all tasks concurrently. Returns a std::tuple of their results.
/// If any child cancels (without InterceptCancel), all siblings are cancelled
/// and cancellation propagates to the awaiting task.
template <typename... Tasks>
class when_all : public aggregate_op {
public:
    template <typename... U>
        requires (sizeof...(U) == sizeof...(Tasks)) && (detail::owned_awaitable<U> && ...)
    explicit when_all(U&&... asyncs) :
        aggregate_op(async_node::NodeKind::WhenAll),
        tasks(detail::normalize_task(std::forward<U>(asyncs))...) {}

    ~when_all() {
        detail::release_inflight_all(tasks);
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
        detail::add_awaitees_all(awaitees, tasks);
        return arm_and_resume(awaiter_handle, location, [this] { return pending_cancel; });
    }

    auto await_resume() {
        return collect(std::index_sequence_for<Tasks...>{});
    }

private:
    template <std::size_t... I>
    auto collect(std::index_sequence<I...>) {
        return std::tuple(detail::take_result(std::get<I>(tasks))...);
    }

    std::tuple<Tasks...> tasks;
};

template <typename Task>
class when_all<detail::range_tasks<Task>> : public aggregate_op {
public:
    template <detail::owned_async_range Range>
        requires std::same_as<detail::normalized_range_task_t<Range>, Task>
    explicit when_all(Range&& range) :
        aggregate_op(async_node::NodeKind::WhenAll),
        tasks(detail::normalize_task_range(std::forward<Range>(range))) {}

    ~when_all() {
        detail::release_inflight_range(tasks);
    }

    bool await_ready() const noexcept {
        return tasks.empty();
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> awaiter_handle,
                      std::source_location location = std::source_location::current()) noexcept {
        total = tasks.size();
        awaitees.clear();
        awaitees.reserve(total);
        detail::add_awaitees_range(awaitees, tasks);
        return arm_and_resume(awaiter_handle, location, [this] { return pending_cancel; });
    }

    auto await_resume() {
        small_vector<detail::task_result_t<Task>> results;
        results.reserve(tasks.size());
        for(auto& task: tasks) {
            results.emplace_back(detail::take_result(task));
        }
        return results;
    }

private:
    small_vector<Task> tasks;
};

/// Awaits the first task to complete. Returns the winner's result.
/// All other tasks are cancelled.
template <typename... Tasks>
class when_any : public aggregate_op {
public:
    template <typename... U>
        requires (sizeof...(U) == sizeof...(Tasks)) && (detail::owned_awaitable<U> && ...)
    explicit when_any(U&&... asyncs) :
        aggregate_op(async_node::NodeKind::WhenAny),
        tasks(detail::normalize_task(std::forward<U>(asyncs))...) {}

    ~when_any() {
        detail::release_inflight_all(tasks);
    }

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> awaiter_handle,
                      std::source_location location = std::source_location::current()) noexcept {
        total = sizeof...(Tasks);
        awaitees.clear();
        awaitees.reserve(total);
        detail::add_awaitees_all(awaitees, tasks);
        return arm_and_resume(awaiter_handle, location, [this] {
            return done || pending_resume || pending_cancel;
        });
    }

    auto await_resume() -> std::variant<detail::task_result_t<Tasks>...> {
        assert(winner != aggregate_op::npos && "when_any winner not set");
        return collect_winner<>();
    }

private:
    template <std::size_t I = 0>
    auto collect_winner() -> std::variant<detail::task_result_t<Tasks>...> {
        if constexpr(I < sizeof...(Tasks)) {
            if(winner == I) {
                return std::variant<detail::task_result_t<Tasks>...>(
                    std::in_place_index<I>,
                    detail::take_result(std::get<I>(tasks)));
            }
            return collect_winner<I + 1>();
        } else {
            std::abort();
        }
    }

    std::tuple<Tasks...> tasks;
};

template <>
class when_any<> {
public:
    when_any() = delete;
};

template <typename Task>
class when_any<detail::range_tasks<Task>> : public aggregate_op {
public:
    template <detail::owned_async_range Range>
        requires std::same_as<detail::normalized_range_task_t<Range>, Task>
    explicit when_any(Range&& range) :
        aggregate_op(async_node::NodeKind::WhenAny),
        tasks(detail::normalize_task_range(std::forward<Range>(range))) {
        assert(!tasks.empty() && "when_any(range) requires a non-empty range");
    }

    ~when_any() {
        detail::release_inflight_range(tasks);
    }

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> awaiter_handle,
                      std::source_location location = std::source_location::current()) noexcept {
        total = tasks.size();
        awaitees.clear();
        awaitees.reserve(total);
        detail::add_awaitees_range(awaitees, tasks);
        return arm_and_resume(awaiter_handle, location, [this] {
            return done || pending_resume || pending_cancel;
        });
    }

    auto await_resume() -> std::pair<std::size_t, detail::task_result_t<Task>> {
        assert(winner != aggregate_op::npos && "when_any winner not set");
        return {winner, detail::take_result(tasks[winner])};
    }

private:
    small_vector<Task> tasks;
};

template <typename... Tasks>
    requires (detail::owned_awaitable<Tasks> && ...)
when_all(Tasks&&...) -> when_all<detail::normalized_task_t<Tasks&&>...>;

template <detail::owned_async_range Range>
when_all(Range&&) -> when_all<detail::range_tasks<detail::normalized_range_task_t<Range>>>;

template <typename... Tasks>
    requires (sizeof...(Tasks) > 0) && (detail::owned_awaitable<Tasks> && ...)
when_any(Tasks&&...) -> when_any<detail::normalized_task_t<Tasks&&>...>;

template <detail::owned_async_range Range>
when_any(Range&&) -> when_any<detail::range_tasks<detail::normalized_range_task_t<Range>>>;

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

    template <typename Awaitable>
        requires (!detail::is_task_v<Awaitable>) && detail::owned_awaitable<Awaitable>
    void spawn(Awaitable&& awaitable) {
        spawn(detail::normalize_task(std::forward<Awaitable>(awaitable)));
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

}  // namespace eventide
