#pragma once

#include <cassert>
#include <cstddef>
#include <optional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "eventide/common/meta.h"
#include "eventide/common/small_vector.h"
#include "eventide/common/type_list.h"
#include "eventide/async/runtime/frame.h"
#include "eventide/async/runtime/task.h"
#include "eventide/async/vocab/outcome.h"

namespace eventide {

namespace detail {

template <typename Task>
using task_error_type_t = typename Task::error_type;

template <typename Task>
using task_cancel_type_t = typename Task::cancel_type;

template <typename List>
struct type_list_to_aggregate;

template <>
struct type_list_to_aggregate<type_list<>> {
    using type = void;
};

template <typename T>
struct type_list_to_aggregate<type_list<T>> {
    using type = T;
};

template <typename... Ts>
struct type_list_to_aggregate<type_list<Ts...>> {
    using type = std::variant<Ts...>;
};

template <typename T>
struct keep_non_void : std::bool_constant<!std::is_void_v<T>> {};

template <typename... Ts>
using aggregated_channel_t = typename type_list_to_aggregate<
    type_list_unique_t<type_list_filter_t<type_list<Ts...>, keep_non_void>>>::type;

template <typename T>
using promote_void_cancel_t = std::conditional_t<std::is_void_v<T>, cancellation, T>;

template <typename... Ts>
constexpr inline bool any_non_void_v = (!std::is_void_v<Ts> || ...);

template <typename... Ts>
using aggregated_cancel_t = std::
    conditional_t<any_non_void_v<Ts...>, aggregated_channel_t<promote_void_cancel_t<Ts>...>, void>;

template <typename Task>
struct range_tasks {
    using task_type = Task;
};

template <typename Task>
using task_result_t = decltype(std::declval<Task&>().result());

template <typename Range>
using range_async_value_t = std::ranges::range_value_t<Range>;

template <typename Range>
using normalized_range_task_t = normalized_task_t<range_async_value_t<Range>>;

template <typename Range>
concept async_range = std::ranges::input_range<Range> && awaitable<range_async_value_t<Range>>;

template <typename Success, typename E, typename C>
using aggregate_result_t =
    std::conditional_t<std::is_void_v<E> && std::is_void_v<C>, Success, outcome<Success, E, C>>;

template <bool CaptureCancel, typename Result>
auto strip_channels_from_result(Result&& result) {
    return std::forward<Result>(result);
}

template <bool CaptureCancel, typename T, typename E, typename C>
auto strip_channels_from_result(outcome<T, E, C>&& result) {
    using type = std::conditional_t<std::is_void_v<C> || CaptureCancel,
                                    std::conditional_t<std::is_void_v<T>, std::nullopt_t, T>,
                                    outcome<T, void, C>>;

    if constexpr(!std::is_void_v<E>) {
        assert(!result.has_error());
    }

    if constexpr(!std::is_void_v<C>) {
        if constexpr(!CaptureCancel) {
            if(result.is_cancelled()) {
                return type(outcome_cancel(C{}));
            }
        } else {
            assert(!result.is_cancelled());
        }
    }

    if constexpr(std::is_void_v<T>) {
        if constexpr(std::is_void_v<C> || CaptureCancel) {
            return std::nullopt;
        } else {
            return type();
        }
    } else {
        if constexpr(std::is_void_v<C> || CaptureCancel) {
            return std::move(*result);
        } else {
            return type(std::move(*result));
        }
    }
}

template <typename Task, bool CaptureCancel>
using task_success_t =
    decltype(strip_channels_from_result<CaptureCancel>(std::declval<task_result_t<Task>>()));

template <typename Task>
async_node* node_from(Task& task) {
    return task.operator->();
}

template <typename Task>
auto take_result(Task& task) {
    return task.result();
}

template <bool CaptureCancel, typename Task>
auto take_success_result(Task& task) {
    return strip_channels_from_result<CaptureCancel>(take_result(task));
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

enum class when_kind { all, any };

template <typename Tuple, typename F>
void tuple_visit_at(std::size_t index, Tuple& tuple, F&& f) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (void)((index == I &&
                (f(std::integral_constant<std::size_t, I>{}, std::get<I>(tuple)), true)) ||
               ...);
    }(std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<Tuple>>>{});
}

}  // namespace detail

// --- Unified variadic when combinator ---

template <detail::when_kind Kind, typename... Tasks>
class when_op : public aggregate_op {
    constexpr static bool capture_cancel =
        !std::is_void_v<detail::aggregated_cancel_t<detail::task_cancel_type_t<Tasks>...>>;

public:
    using error_type = detail::aggregated_channel_t<detail::task_error_type_t<Tasks>...>;
    using cancel_type = detail::aggregated_cancel_t<detail::task_cancel_type_t<Tasks>...>;

    using success_type =
        std::conditional_t<Kind == detail::when_kind::all,
                           std::tuple<detail::task_success_t<Tasks, capture_cancel>...>,
                           std::variant<detail::task_success_t<Tasks, capture_cancel>...>>;

    using result_type = detail::aggregate_result_t<success_type, error_type, cancel_type>;

    template <detail::awaitable... U>
    explicit when_op(U... asyncs) :
        aggregate_op(Kind == detail::when_kind::all ? async_node::NodeKind::WhenAll
                                                    : async_node::NodeKind::WhenAny),
        tasks(detail::normalize_task(std::move(asyncs))...) {
        static_assert(Kind == detail::when_kind::all || sizeof...(Tasks) > 0,
                      "when_any requires at least one task");
        if constexpr(!std::is_void_v<cancel_type>) {
            this->intercept_cancel();
        }
    }

    ~when_op() {
        std::apply([](auto&... ts) { (detail::release_inflight(ts), ...); }, tasks);
    }

    bool await_ready() const noexcept {
        if constexpr(Kind == detail::when_kind::all) {
            return sizeof...(Tasks) == 0;
        } else {
            return false;
        }
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> awaiter_handle,
                      std::source_location location = std::source_location::current()) noexcept {
        total = sizeof...(Tasks);
        awaitees.clear();
        awaitees.reserve(total);
        std::apply([&](auto&... ts) { (awaitees.push_back(detail::node_from(ts)), ...); }, tasks);
        return arm_and_resume(awaiter_handle, location);
    }

    auto await_resume() -> result_type {
        rethrow_if_propagated();

        if constexpr(!std::is_void_v<cancel_type>) {
            if(this->state == async_node::Cancelled) {
                std::optional<cancel_type> cancel;
                detail::tuple_visit_at(first_cancel_child, tasks, [&](auto, auto& task) {
                    using task_t = std::remove_reference_t<decltype(task)>;
                    if constexpr(std::is_void_v<detail::task_cancel_type_t<task_t>>) {
                        cancel.emplace(cancellation{});
                    } else {
                        auto result = detail::take_result(task);
                        assert(result.is_cancelled());
                        cancel.emplace(std::move(result).cancellation());
                    }
                });
                assert(cancel);
                return result_type(outcome_cancel(std::move(*cancel)));
            }
        }

        if constexpr(!std::is_void_v<error_type>) {
            if(first_error_child != aggregate_op::npos) {
                std::optional<error_type> error;
                detail::tuple_visit_at(first_error_child, tasks, [&](auto, auto& task) {
                    using task_t = std::remove_reference_t<decltype(task)>;
                    if constexpr(!std::is_void_v<detail::task_error_type_t<task_t>>) {
                        auto result = detail::take_result(task);
                        assert(result.has_error());
                        error.emplace(std::move(result).error());
                    }
                });
                assert(error);
                return result_type(outcome_error(std::move(*error)));
            }
        }

        auto success = collect_success();
        if constexpr(std::same_as<result_type, success_type>) {
            return std::move(success);
        } else {
            return result_type(std::move(success));
        }
    }

private:
    auto collect_success() -> success_type {
        if constexpr(Kind == detail::when_kind::all) {
            return [this]<std::size_t... I>(std::index_sequence<I...>) {
                return success_type(
                    detail::take_success_result<capture_cancel>(std::get<I>(tasks))...);
            }(std::index_sequence_for<Tasks...>{});
        } else {
            assert(winner != aggregate_op::npos && "when_any winner not set");
            std::optional<success_type> result;
            detail::tuple_visit_at(winner, tasks, [&](auto I, auto& task) {
                result = success_type(std::in_place_index<I.value>,
                                      detail::take_success_result<capture_cancel>(task));
            });
            assert(result);
            return std::move(*result);
        }
    }

    std::tuple<Tasks...> tasks;
};

// --- Unified range when combinator ---

template <detail::when_kind Kind, typename Task>
class when_op<Kind, detail::range_tasks<Task>> : public aggregate_op {
    constexpr static bool capture_cancel = !std::is_void_v<detail::task_cancel_type_t<Task>>;

public:
    using error_type = detail::task_error_type_t<Task>;
    using cancel_type = detail::task_cancel_type_t<Task>;

    using success_type =
        std::conditional_t<Kind == detail::when_kind::all,
                           small_vector<detail::task_success_t<Task, capture_cancel>>,
                           std::pair<std::size_t, detail::task_success_t<Task, capture_cancel>>>;

    using result_type = detail::aggregate_result_t<success_type, error_type, cancel_type>;

    template <detail::async_range Range>
        requires std::same_as<detail::normalized_range_task_t<Range>, Task>
    explicit when_op(Range range) :
        aggregate_op(Kind == detail::when_kind::all ? async_node::NodeKind::WhenAll
                                                    : async_node::NodeKind::WhenAny) {
        if constexpr(std::ranges::sized_range<Range>) {
            tasks.reserve(std::ranges::size(range));
        }
        for(auto&& async: range) {
            tasks.emplace_back(detail::normalize_task(std::move(async)));
        }
        if constexpr(Kind == detail::when_kind::any) {
            assert(!tasks.empty() && "when_any(range) requires a non-empty range");
        }
        if constexpr(!std::is_void_v<cancel_type>) {
            this->intercept_cancel();
        }
    }

    ~when_op() {
        for(auto& task: tasks) {
            detail::release_inflight(task);
        }
    }

    bool await_ready() const noexcept {
        if constexpr(Kind == detail::when_kind::all) {
            return tasks.empty();
        } else {
            return false;
        }
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> awaiter_handle,
                      std::source_location location = std::source_location::current()) noexcept {
        total = tasks.size();
        awaitees.clear();
        awaitees.reserve(total);
        for(auto& task: tasks) {
            awaitees.push_back(detail::node_from(task));
        }
        return arm_and_resume(awaiter_handle, location);
    }

    auto await_resume() -> result_type {
        rethrow_if_propagated();

        if constexpr(!std::is_void_v<cancel_type>) {
            if(this->state == async_node::Cancelled) {
                cancel_type cancel;
                if constexpr(std::is_void_v<detail::task_cancel_type_t<Task>>) {
                    cancel = cancellation{};
                } else {
                    auto result = detail::take_result(tasks[first_cancel_child]);
                    assert(result.is_cancelled());
                    cancel = std::move(result).cancellation();
                }
                return result_type(outcome_cancel(std::move(cancel)));
            }
        }

        if constexpr(!std::is_void_v<error_type>) {
            if(first_error_child != aggregate_op::npos) {
                auto result = detail::take_result(tasks[first_error_child]);
                assert(result.has_error());
                return result_type(outcome_error(error_type(std::move(result).error())));
            }
        }

        auto success = collect_success();
        if constexpr(std::same_as<result_type, success_type>) {
            return std::move(success);
        } else {
            return result_type(std::move(success));
        }
    }

private:
    auto collect_success() -> success_type {
        if constexpr(Kind == detail::when_kind::all) {
            success_type results;
            results.reserve(tasks.size());
            for(auto& task: tasks) {
                results.emplace_back(detail::take_success_result<capture_cancel>(task));
            }
            return results;
        } else {
            assert(winner != aggregate_op::npos && "when_any winner not set");
            return success_type{winner, detail::take_success_result<capture_cancel>(tasks[winner])};
        }
    }

    small_vector<Task> tasks;
};

// --- Public when_all / when_any wrappers ---

template <typename... Tasks>
class when_all : public when_op<detail::when_kind::all, Tasks...> {
    using when_op<detail::when_kind::all, Tasks...>::when_op;
};

template <typename... Tasks>
class when_any : public when_op<detail::when_kind::any, Tasks...> {
    using when_op<detail::when_kind::any, Tasks...>::when_op;
};

template <>
class when_any<> {
public:
    when_any() = delete;
};

// --- Deduction guides ---

template <detail::awaitable... Tasks>
when_all(Tasks...) -> when_all<detail::normalized_task_t<Tasks>...>;

template <detail::async_range Range>
when_all(Range) -> when_all<detail::range_tasks<detail::normalized_range_task_t<Range>>>;

template <detail::awaitable... Tasks>
    requires (sizeof...(Tasks) > 0)
when_any(Tasks...) -> when_any<detail::normalized_task_t<Tasks>...>;

template <detail::async_range Range>
when_any(Range) -> when_any<detail::range_tasks<detail::normalized_range_task_t<Range>>>;

template <typename... Errors>
class async_scope : public aggregate_op {
public:
    using error_type = detail::aggregated_channel_t<Errors...>;
    using result_type =
        std::conditional_t<std::is_void_v<error_type>, void, outcome<void, error_type, void>>;

    async_scope() : aggregate_op(async_node::NodeKind::Scope) {}

    async_scope(const async_scope&) = delete;
    async_scope& operator=(const async_scope&) = delete;

    ~async_scope() {
        for(auto* node: awaitees) {
            if(node) {
                detail::destroy_or_detach(node);
            }
        }
    }

    template <typename T, typename E, typename C>
        requires std::is_void_v<E> || is_one_of<E, Errors...>
    void spawn(task<T, E, C>&& t) {
        auto* node = detail::node_from(t);
        t.release();
        if constexpr(!std::is_void_v<error_type>) {
            if constexpr(std::is_void_v<E>) {
                error_extractors.push_back(nullptr);
            } else {
                error_extractors.push_back([](async_node* current) -> error_type {
                    using handle_type = typename task<T, E, C>::coroutine_handle;
                    auto handle = handle_type::from_address(
                        static_cast<standard_task*>(current)->handle().address());
                    auto& promise = handle.promise();
                    assert(promise.value.has_value());
                    assert(promise.value->has_error());
                    return error_type(std::move(*promise.value).error());
                });
            }
        }
        awaitees.push_back(node);
        total += 1;
    }

    template <detail::awaitable Awaitable>
        requires (!detail::is_task_v<Awaitable>) &&
                 std::is_void_v<detail::task_error_type_t<detail::normalized_task_t<Awaitable>>>
    void spawn(Awaitable awaitable) {
        spawn(detail::normalize_task(std::move(awaitable)));
    }

    bool await_ready() const noexcept {
        return awaitees.empty();
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> awaiter_handle,
                      std::source_location location = std::source_location::current()) noexcept {
        total = awaitees.size();
        return arm_and_resume(awaiter_handle, location);
    }

    auto await_resume() -> result_type {
        rethrow_if_propagated();

        if constexpr(!std::is_void_v<error_type>) {
            if(first_error_child != aggregate_op::npos) {
                return result_type(outcome_error(
                    error_extractors[first_error_child](awaitees[first_error_child])));
            }
            return result_type();
        }
    }

private:
    std::vector<error_type (*)(async_node*)> error_extractors;
};

async_scope() -> async_scope<>;

}  // namespace eventide
