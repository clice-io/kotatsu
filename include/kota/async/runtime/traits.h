#pragma once

#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>

#include "kota/support/type_list.h"
#include "kota/support/type_traits.h"
#include "kota/async/runtime/awaitable.h"
#include "kota/async/runtime/task.h"
#include "kota/async/vocab/outcome.h"

namespace kota::detail {

template <typename T>
constexpr inline bool is_task_v = is_specialization_of<task, T>;

/// The task when_all and when_any run for an awaitable: the awaitable itself
/// when it is a task.
template <typename Awaitable>
struct normalized_task;

template <awaitable Awaitable>
    requires (!is_task_v<Awaitable>)
struct normalized_task<Awaitable> {
    using type = task<await_result_t<Awaitable&&>>;
};

template <typename T, typename E, typename C>
struct normalized_task<task<T, E, C>> {
    using type = task<T, E, C>;
};

template <typename Awaitable>
using normalized_task_t = typename normalized_task<Awaitable>::type;

template <typename T, typename E, typename C>
task<T, E, C> normalize(task<T, E, C> t) {
    return t;
}

// The frame keeps the awaitable, whose type may be local to the caller's
// translation unit; GCC would warn that the frame type uses it.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsubobject-linkage"
#endif

template <awaitable Awaitable>
    requires (!is_task_v<Awaitable>)
normalized_task_t<Awaitable> normalize(Awaitable awaitable) {
    co_return co_await std::move(awaitable);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

template <typename T>
struct keep_non_void : std::bool_constant<!std::is_void_v<T>> {};

/// The one channel type for several: void when none of them has one, the type
/// when they share one, a variant of the distinct types otherwise.
template <typename... Ts>
using merged_channel_t =
    type_list_to_union_t<type_list_unique_t<type_list_filter_t<type_list<Ts...>, keep_non_void>>>;

/// What a child that succeeded contributes to its aggregate's value.
template <typename Task>
using success_t = std::conditional_t<std::is_void_v<typename Task::value_type>,
                                     std::nullopt_t,
                                     typename Task::value_type>;

template <typename Success, typename E, typename C>
using aggregate_result_t =
    std::conditional_t<std::is_void_v<E> && std::is_void_v<C>, Success, outcome<Success, E, C>>;

/// The task type of a range handed to when_all or when_any.
template <typename Task>
struct range_tasks {};

template <typename Range>
concept async_range =
    std::ranges::input_range<Range> && awaitable<std::ranges::range_value_t<Range>>;

template <typename Range>
using normalized_range_task_t = normalized_task_t<std::ranges::range_value_t<Range>>;

}  // namespace kota::detail
