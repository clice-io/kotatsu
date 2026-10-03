#pragma once

#include <cassert>
#include <coroutine>
#include <exception>
#include <source_location>
#include <type_traits>
#include <vector>

#include "kota/support/config.h"
#include "kota/support/type_list.h"
#include "kota/async/runtime/node.h"
#include "kota/async/runtime/task.h"
#include "kota/async/runtime/traits.h"
#include "kota/async/vocab/outcome.h"

namespace kota {

/// Runs a dynamic set of child tasks that all end before join() returns. A
/// child starts at once and runs until it first suspends. A child that fails
/// cancels its siblings, and join() reports the failure; a child that ends
/// cancelled just ends, and its siblings run on. The group keeps no child that
/// has ended. with_task_group() keeps a group where it cannot go before its
/// children.
template <typename... Errors>
class task_group : aggregate_op {
public:
    using error_type = detail::merged_channel_t<Errors...>;

    /// What join() gives: nothing when no child can fail with an error, or the
    /// errors of the children that failed, in the order they failed.
    using result_type = std::
        conditional_t<std::is_void_v<error_type>, void, outcome<void, std::vector<error_type>>>;

    task_group() noexcept : aggregate_op(NodeKind::TaskGroup) {}

    task_group(const task_group&) = delete;
    task_group& operator=(const task_group&) = delete;

    /// Children still running are let go: each is cancelled and ends on its
    /// own, and what it failed with is dropped. Until then they may still use
    /// what they reference; co_await join() to wait for them. A child the
    /// cancel ends at once, or that catches the cancellation and runs on to
    /// its next suspending co_await, does so before the destructor returns;
    /// spawn() refuses meanwhile.
    ~task_group() {
        abandon_children();
    }

    /// Starts `child` and runs it until it first suspends. Refused, returning
    /// false, once the children are being cancelled (by cancel(), by a child
    /// that failed, or by a cancel of the task awaiting join()) or join() has
    /// returned.
    template <typename T, typename E, typename C>
        requires std::is_void_v<E> || is_one_of<E, Errors...>
    bool spawn(task<T, E, C>&& child,
               std::source_location location = std::source_location::current()) {
        if(decided() || done()) {
            return false;
        }
        auto& frame = detail::task_access::release(child);
        if constexpr(std::is_void_v<E>) {
            aggregate_op::spawn(frame, location);
        } else {
            aggregate_op::spawn(watch(frame, &take_error<T, E>), location);
        }
        return true;
    }

    /// Cancels every child. join() returns as usual once they have ended.
    void cancel() {
        if(decided() || done()) {
            return;
        }
        decision = Decision::Resume;
        resume_and_drain(cancel_all());
    }

    /// Waits until every child has ended, then rethrows the first exception a
    /// child threw, or gives the errors. Awaited by a task that gets
    /// cancelled, it cancels the children, waits for them all the same, and
    /// the task ends cancelled unless a child failed. Awaited once.
    auto join() noexcept {
        return join_awaiter{*this};
    }

private:
    struct join_awaiter {
        task_group& group;

        bool await_ready() noexcept {
            assert(group.state == State::Pending && "join() awaited twice");
            if(group.pending != 0) {
                return false;
            }
            group.state = group.settled_state();
            return true;
        }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<Promise> waiting,
            std::source_location location = std::source_location::current()) noexcept {
            return group.await_children(waiting.promise(), location);
        }

        result_type await_resume() {
#if KOTA_ENABLE_EXCEPTIONS
            if(group.exception) {
                std::rethrow_exception(group.exception);
            }
#endif
            if constexpr(!std::is_void_v<error_type>) {
                if(!group.errors.empty()) {
                    return result_type(outcome_error(std::move(group.errors)));
                }
                return result_type();
            }
        }
    };

    template <typename T, typename E>
    static void take_error(task_frame& child, async_node& group) {
        static_cast<task_group&>(group).errors.emplace_back(
            static_cast<typename task<T, E>::promise_type&>(child).take_error());
    }

    std::
        conditional_t<std::is_void_v<error_type>, std::type_identity<void>, std::vector<error_type>>
            errors;
};

namespace detail {

/// What a with_task_group() body may return: a task without a value that a
/// task_group<Errors...> takes.
template <typename Task, typename... Errors>
constexpr inline bool is_group_body_v = false;

template <typename E, typename... Errors>
constexpr inline bool is_group_body_v<task<void, E>, Errors...> =
    std::is_void_v<E> || is_one_of<E, Errors...>;

/// The error channel of with_task_group(): the errors join() gives, or none
/// when no child can fail with an error.
template <typename E>
using join_errors_t = std::conditional_t<std::is_void_v<E>, void, std::vector<E>>;

}  // namespace detail

/// Runs `body` as the first child of a task_group of its own, and ends once
/// every child has ended, `body` and what it spawned alike. Awaited, it gives
/// what join() gives. The group and `body` live in this task's frame, so both
/// outlive every child, which may use what `body` captures; a group a task
/// keeps itself lets its children go if it goes first. `body` is a child like
/// the others: if it fails, it cancels them; if it ends cancelled, it just
/// ends. A cancel of the task awaiting this one cancels every child, as
/// through join().
///
///   co_await with_task_group([&](task_group<>& group) -> task<> {
///       for(auto& request: requests) {
///           group.spawn(answer(request));
///       }
///       co_return;
///   });
///
template <typename... Errors, typename Body>
    requires detail::is_group_body_v<std::invoke_result_t<Body&, task_group<Errors...>&>, Errors...>
task<void, detail::join_errors_t<typename task_group<Errors...>::error_type>>
    with_task_group(Body body, std::source_location location = std::source_location::current()) {
    task_group<Errors...> group;
    // A group that has just been made takes any child.
    group.spawn(body(group), location);
    if constexpr(std::is_void_v<typename task_group<Errors...>::error_type>) {
        co_await group.join();
    } else {
        co_await or_fail(co_await group.join());
    }
}

}  // namespace kota
