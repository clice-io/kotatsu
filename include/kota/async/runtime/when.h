#pragma once

#include <cstddef>
#include <optional>
#include <ranges>
#include <source_location>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/support/config.h"
#include "kota/support/small_vector.h"
#include "kota/async/runtime/node.h"
#include "kota/async/runtime/traits.h"
#include "kota/async/vocab/error.h"
#include "kota/async/vocab/outcome.h"

namespace kota {

namespace detail {

/// How when_all and when_any hold their children: a tuple, or a vector for a
/// range.
template <typename... Tasks>
struct when_children {
    using type = std::tuple<Tasks...>;
};

template <typename Task>
struct when_children<range_tasks<Task>> {
    using type = small_vector<Task>;
};

/// The shared body of when_all and when_any.
template <bool All, typename Children>
class when_op : aggregate_op {
    constexpr static bool is_range = !is_specialization_of<std::tuple, Children>;

    template <typename C>
    struct channels;

    template <typename... Tasks>
    struct channels<std::tuple<Tasks...>> {
        using error_type = merged_channel_t<typename Tasks::error_type...>;
        using cancel_type = merged_channel_t<typename Tasks::cancel_type...>;
        using success_type = std::
            conditional_t<All, std::tuple<success_t<Tasks>...>, std::variant<success_t<Tasks>...>>;
    };

    template <typename Task>
    struct channels<small_vector<Task>> {
        using error_type = typename Task::error_type;
        using cancel_type = typename Task::cancel_type;
        using success_type = std::conditional_t<All,
                                                small_vector<success_t<Task>>,
                                                std::pair<std::size_t, success_t<Task>>>;
    };

public:
    using error_type = typename channels<Children>::error_type;
    using cancel_type = typename channels<Children>::cancel_type;
    using success_type = typename channels<Children>::success_type;
    using result_type = aggregate_result_t<success_type, error_type, cancel_type>;

    template <awaitable... Awaitables>
        requires (!is_range)
    explicit when_op(Awaitables... awaitables) :
        aggregate_op(All ? NodeKind::WhenAll : NodeKind::WhenAny),
        tasks(normalize(std::move(awaitables))...) {
        intercept = !std::is_void_v<cancel_type>;
    }

    template <async_range Range>
        requires is_range
    explicit when_op(Range range) : aggregate_op(All ? NodeKind::WhenAll : NodeKind::WhenAny) {
        if constexpr(std::ranges::sized_range<Range>) {
            tasks.reserve(std::ranges::size(range));
        }
        for(auto&& awaitable: range) {
            tasks.emplace_back(normalize(std::move(awaitable)));
        }
        if(!All && tasks.empty()) {
            KOTA_THROW(std::invalid_argument("when_any(range) requires a non-empty range"));
        }
        intercept = !std::is_void_v<cancel_type>;
    }

    bool await_ready() const noexcept {
        if constexpr(!All) {
            return false;
        } else if constexpr(is_range) {
            return tasks.empty();
        } else {
            return std::tuple_size_v<Children> == 0;
        }
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        small_vector<task_frame*> children;
        for_each_task([&]<typename Task>(Task& child) {
            children.push_back(&watch(task_access::promise(child), hook_for<Task>()));
        });
        return arm(waiting.promise(), {children.data(), children.size()}, location);
    }

    result_type await_resume() {
#if KOTA_ENABLE_EXCEPTIONS
        if(exception) {
            std::rethrow_exception(exception);
        }
#endif
        if constexpr(!std::is_void_v<error_type>) {
            if(first_error) {
                return result_type(outcome_error(std::move(*first_error)));
            }
        }
        if constexpr(!std::is_void_v<cancel_type>) {
            if(state == State::Cancelled) {
                return result_type(outcome_cancel(cancellation{}));
            }
        }
        return result_type(collect_success());
    }

private:
    template <typename F>
    void for_each_task(F&& f) {
        if constexpr(is_range) {
            for(auto& child: tasks) {
                f(child);
            }
        } else {
            std::apply([&](auto&... children) { (f(children), ...); }, tasks);
        }
    }

    /// Keeps the first error a child fails with.
    template <typename Task>
    static void take_error(task_frame& child, async_node& op) {
        auto& self = static_cast<when_op&>(op);
        if(!self.first_error) {
            self.first_error.emplace(static_cast<typename Task::promise_type&>(child).take_error());
        }
    }

    template <typename Task>
    static error_hook hook_for() noexcept {
        if constexpr(std::is_void_v<typename Task::error_type>) {
            return nullptr;
        } else {
            return &take_error<Task>;
        }
    }

    template <typename Task>
    static auto success_of(Task& child) {
        if constexpr(std::is_void_v<typename Task::value_type>) {
            return std::nullopt;
        } else {
            return std::move(*task_access::promise(child).take());
        }
    }

    template <typename Task>
    bool won(Task& child) const noexcept {
        return winner == &task_access::promise(child);
    }

    success_type collect_success() {
        if constexpr(All && is_range) {
            success_type values;
            values.reserve(tasks.size());
            for(auto& child: tasks) {
                values.emplace_back(success_of(child));
            }
            return values;
        } else if constexpr(All) {
            return std::apply(
                [](auto&... children) { return success_type(success_of(children)...); },
                tasks);
        } else if constexpr(is_range) {
            for(std::size_t index = 0; index < tasks.size(); ++index) {
                if(won(tasks[index])) {
                    return success_type(index, success_of(tasks[index]));
                }
            }
            std::unreachable();
        } else {
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                std::optional<success_type> value;
                ((won(std::get<I>(tasks)) &&
                  (value.emplace(std::in_place_index<I>, success_of(std::get<I>(tasks))), true)) ||
                 ...);
                return std::move(*value);
            }(std::make_index_sequence<std::tuple_size_v<Children>>{});
        }
    }

    Children tasks;

    /// The error of the first child that failed with one.
    std::conditional_t<std::is_void_v<error_type>,
                       std::type_identity<void>,
                       std::optional<error_type>>
        first_error;
};

}  // namespace detail

/// Awaits all tasks concurrently, collecting results into a tuple.
///
/// Variadic overload: accepts heterogeneous awaitables (tasks, semaphore acquires, etc.)
/// and returns `std::tuple<T...>` where each element is the result of the corresponding task.
/// Void tasks produce `std::nullopt_t` in the tuple.
///
/// Range overload: accepts a range of homogeneous tasks and returns `small_vector<T>`.
///
/// A child that fails or ends cancelled cancels the others, as a cancel of the
/// awaiting task does. The result then ranks what happened, highest first: an
/// exception a child threw, rethrown; the first error a child failed with,
/// even while it was being cancelled; a cancellation. An error comes back in
/// `outcome<..., E, ...>`, and a cancellation too when a child has a cancel
/// channel; otherwise the awaiting task ends cancelled as well.
///
/// All children are guaranteed to have completed before the aggregate returns
/// (structured completion).
template <typename... Tasks>
class when_all : private detail::when_op<true, typename detail::when_children<Tasks...>::type> {
    using base = detail::when_op<true, typename detail::when_children<Tasks...>::type>;

public:
    using base::base;
    using base::await_ready;
    using base::await_suspend;
    using base::await_resume;
};

/// Awaits multiple tasks concurrently, returning the first to complete.
///
/// Variadic overload: accepts heterogeneous awaitables and returns
/// `std::variant<T...>` where the active alternative corresponds to the winning task.
/// Void tasks produce `std::nullopt_t` in the variant.
///
/// Range overload: accepts a range of homogeneous tasks and returns
/// `std::pair<std::size_t, T>` where the first element is the index of the winner.
/// An empty range throws std::invalid_argument.
///
/// The first child to end cancels the others, as a cancel of the awaiting task
/// does: one that succeeds wins, and one that fails or ends cancelled ends the
/// race. The result then ranks what happened, highest first: an exception a
/// child threw, rethrown; the first error a child failed with, even while it
/// was being cancelled after another won; what the first child to end
/// decided, its value or a cancellation. An error comes back in
/// `outcome<..., E, ...>`, and a cancellation too when a child has a cancel
/// channel; otherwise the awaiting task ends cancelled as well.
///
/// All siblings are guaranteed to have completed before the aggregate returns
/// (structured completion).
template <typename... Tasks>
class when_any : private detail::when_op<false, typename detail::when_children<Tasks...>::type> {
    using base = detail::when_op<false, typename detail::when_children<Tasks...>::type>;

public:
    using base::base;
    using base::await_ready;
    using base::await_suspend;
    using base::await_resume;
};

/// when_any needs at least one task to wait for.
template <>
class when_any<> {
public:
    when_any() = delete;
};

template <detail::awaitable... Awaitables>
when_all(Awaitables...) -> when_all<detail::normalized_task_t<Awaitables>...>;

template <detail::async_range Range>
when_all(Range) -> when_all<detail::range_tasks<detail::normalized_range_task_t<Range>>>;

template <detail::awaitable... Awaitables>
when_any(Awaitables...) -> when_any<detail::normalized_task_t<Awaitables>...>;

template <detail::async_range Range>
when_any(Range) -> when_any<detail::range_tasks<detail::normalized_range_task_t<Range>>>;

}  // namespace kota
