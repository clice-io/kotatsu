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

#include "kota/support/config.h"
#include "kota/async/io/loop.h"
#include "kota/async/runtime/node.h"
#include "kota/async/vocab/error.h"
#include "kota/async/vocab/outcome.h"

namespace kota {

template <typename T, typename E>
struct task_promise_object;

namespace detail {

/// How the library reaches into tasks and their frames.
struct task_access {
    template <typename Task>
    static auto& promise(Task& task) noexcept {
        return task.h.promise();
    }

    /// Takes the frame out of `task`, which no longer owns it.
    template <typename Task>
    static auto& release(Task& task) noexcept {
        return std::exchange(task.h, nullptr).promise();
    }

    /// Starts `child` as the task `waiting` awaits.
    template <typename Task>
    static std::coroutine_handle<> await(task_frame& waiting,
                                         Task& child,
                                         task_frame::error_hook hook,
                                         std::source_location location) {
        return waiting.await_task(child.h.promise(),
                                  !std::is_void_v<typename Task::cancel_type>,
                                  hook,
                                  location);
    }

    /// Readies `task` to start as a root, and hands its frame over to the event
    /// loop when `owned`.
    template <typename Task>
    static task_frame& make_root(Task& task, bool owned, std::source_location location) noexcept {
        task_frame& root = task.h.promise();
        assert(root.state == async_node::State::Pending && "a task starts once");
        root.location = location;
        root.root = owned;
        if(owned) {
            task.h = nullptr;
        }
        return root;
    }

    /// Starts a root the event loop scheduled.
    static void run_root(task_frame& root);

    /// Resumes a task a sync primitive woke.
    static void resume_woken(task_frame& task);
};

template <typename Task>
task_frame& make_root(Task& task, bool owned, std::source_location location) noexcept {
    return task_access::make_root(task, owned, location);
}

/// Carrier for task::or_fail(); await_transform turns it into an
/// or_fail_task_await.
template <typename Task>
struct or_fail_proxy {
    Task inner;
};

/// Awaits a task through or_fail(): the child's error ends the awaiting task
/// with that error without resuming it, and a success resumes it with the
/// bare value.
template <typename ParentPromise, typename ChildTask>
struct or_fail_task_await {
    ChildTask child;

    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<ParentPromise> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        return task_access::await(waiting.promise(), child, &propagate, location);
    }

    /// Reached on success only, or to rethrow what the child threw.
    auto await_resume() {
        if constexpr(std::is_void_v<typename ChildTask::value_type>) {
            task_access::promise(child).take();
        } else {
            return std::move(*task_access::promise(child).take());
        }
    }

private:
    static void propagate(task_frame& child, async_node& parent) {
        using error_type = typename ParentPromise::error_type;
        auto& from = static_cast<typename ChildTask::promise_type&>(child);
        static_cast<ParentPromise&>(parent).value.emplace(
            outcome_error(error_type(from.take_error())));
    }
};

}  // namespace detail

/// co_await cancel(): ends the task cancelled at once.
struct cancel_await {};

inline cancel_await cancel() noexcept {
    return {};
}

/// Carrier for or_fail(); transformed by await_transform in the promise.
template <typename Outcome>
struct or_fail_await {
    Outcome result;
};

/// An outcome-like type that carries an error channel (no cancel channel).
template <typename Outcome>
concept or_fail_result = is_outcome_v<std::remove_cvref_t<Outcome>> &&
                         std::is_void_v<typename std::remove_cvref_t<Outcome>::cancel_type> &&
                         (!std::is_void_v<typename std::remove_cvref_t<Outcome>::error_type>);

/// Propagate the error channel of a non-task outcome; resume with its value on success.
///
///   auto value = co_await or_fail(some_result);  // propagates error or unwraps value
///
template <typename Outcome>
    requires or_fail_result<Outcome>
auto or_fail(Outcome&& result) {
    return or_fail_await<std::remove_cvref_t<Outcome>>{std::forward<Outcome>(result)};
}

/// Carrier for fail(); holds forwarding references to the error constructor args.
/// Must be consumed immediately via co_await (same lifetime constraint as
/// std::forward_as_tuple).
template <typename... Args>
struct fail_await {
    std::tuple<Args&&...> args;
};

/// Construct an error and end the current task with it.
///
///   co_await fail(error_code, "message");  // replaces co_return outcome_error(...)
///
template <typename... Args>
auto fail(Args&&... args) {
    return fail_await<Args...>{std::forward_as_tuple(std::forward<Args>(args)...)};
}

/// Where a task's co_return puts its value.
template <typename T, typename E>
struct promise_result {
    std::optional<outcome<T, E>> value;

    template <typename U>
    void return_value(U&& val) noexcept {
        value.emplace(std::forward<U>(val));
    }
};

/// Void-value tasks complete successfully via `co_return;`.
/// Use `co_await fail(...)` or `co_await or_fail(...)` to propagate errors.
template <typename E>
struct promise_result<void, E> {
    std::optional<outcome<void, E>> value;

    void return_void() noexcept {
        value.emplace();
    }
};

template <typename T, typename E>
struct task_return_object {
    std::coroutine_handle<task_promise_object<T, E>> handle;

    template <typename C>
    operator task<T, E, C>() const noexcept {
        return task<T, E, C>(handle);
    }
};

template <typename T, typename E>
struct task_promise_object : task_frame, promise_result<T, E> {
    using error_type = E;

    task_promise_object() noexcept {
        this->address = std::coroutine_handle<task_promise_object>::from_promise(*this).address();
    }

    task_return_object<T, E> get_return_object() noexcept {
        return {std::coroutine_handle<task_promise_object>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() const noexcept {
        return {};
    }

    auto final_suspend() noexcept {
        State end = State::Succeeded;
        if(threw() || has_error()) {
            // Real errors outrank cancellation: a task that fails or throws
            // after it was cancelled still reports the error.
            end = State::Failed;
        } else if(cancel_requested) {
            end = State::Cancelled;
        }
        return finish_await{*this, end};
    }

    void unhandled_exception() noexcept {
#if KOTA_ENABLE_EXCEPTIONS
        exception = std::current_exception();
#else
        std::abort();
#endif
    }

    /// co_await cancel(): end cancelled.
    auto await_transform(cancel_await) noexcept {
        return finish_await{*this, State::Cancelled};
    }

    /// co_await fail(args...): end with the error they make.
    template <typename... Args>
    auto await_transform(fail_await<Args...>&& fail) noexcept
        requires (!std::is_void_v<E>) && std::constructible_from<E, Args...> {
        this->value.emplace(outcome_error(std::apply(
            [](auto&&... forwarded) { return E(std::forward<decltype(forwarded)>(forwarded)...); },
            std::move(fail.args))));
        return finish_await{*this, State::Failed};
    }

    /// co_await or_fail(outcome): end with its error, or resume with its value.
    template <typename Outcome>
    auto await_transform(or_fail_await<Outcome>&& awaited) noexcept
        requires (!std::is_void_v<E>) &&
                 or_fail_result<Outcome> && std::constructible_from<E, typename Outcome::error_type>
    {
        struct awaiter {
            Outcome result;
            /// The task to end; null when the outcome has a value.
            task_promise_object* failing;

            bool await_ready() const noexcept {
                return failing == nullptr;
            }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept {
                return finish_await{*failing, State::Failed}.await_suspend({});
            }

            decltype(auto) await_resume() noexcept {
                if constexpr(!std::is_void_v<typename Outcome::value_type>) {
                    return std::move(*result);
                }
            }
        };

        if(awaited.result.has_error()) {
            this->value.emplace(outcome_error(E(std::move(awaited.result).error())));
            return awaiter{std::move(awaited.result), this};
        }
        return awaiter{std::move(awaited.result), nullptr};
    }

    /// co_await task.or_fail(): end with the child's error without resuming.
    template <typename ChildT, typename ChildE>
    auto await_transform(detail::or_fail_proxy<task<ChildT, ChildE, void>>&& wrapped) noexcept
        requires (!std::is_void_v<E>) && std::constructible_from<E, ChildE> {
        return detail::or_fail_task_await<task_promise_object, task<ChildT, ChildE, void>>{
            std::move(wrapped.inner)};
    }

    /// Pass-through for all other awaitables.
    template <typename Awaitable>
    decltype(auto) await_transform(Awaitable&& awaitable) noexcept {
        return std::forward<Awaitable>(awaitable);
    }

    /// What the task ended with. Rethrows what it threw.
    outcome<T, E, cancellation> take() {
#if KOTA_ENABLE_EXCEPTIONS
        if(exception) {
            std::rethrow_exception(exception);
        }
#endif
        if(state == State::Cancelled) {
            return outcome_cancel(cancellation{});
        }
        assert(this->value.has_value() && "take() on a task that has not finished");
        if constexpr(!std::is_void_v<E>) {
            if(this->value->has_error()) {
                return outcome_error(std::move(*this->value).error());
            }
        }
        if constexpr(std::is_void_v<T>) {
            return {};
        } else {
            return std::move(**this->value);
        }
    }

    /// The error of a task that failed without throwing.
    E take_error()
        requires (!std::is_void_v<E>) {
        return std::move(*this->value).error();
    }

private:
    /// Ends the task in `end` from a suspension point.
    struct finish_await {
        task_promise_object& promise;
        State end;

        bool await_ready() const noexcept {
            return false;
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept {
            return promise.finish(end);
        }

        [[noreturn]] void await_resume() const noexcept {
            std::abort();
        }
    };

    bool has_error() const noexcept {
        if constexpr(std::is_void_v<E>) {
            return false;
        } else {
            return this->value.has_value() && this->value->has_error();
        }
    }
};

/// A lazily started coroutine: it runs once awaited, spawned into a
/// task_group, or scheduled on an event loop. What awaiting it gives follows
/// its channels: T alone when E and C are void, `outcome<T, E>` with an error
/// channel, `outcome<T, E, cancellation>` with a cancel channel. Without a
/// cancel channel a task that ends cancelled cancels the task awaiting it too.
template <typename T, typename E, typename C>
class task {
public:
    static_assert(std::is_void_v<C> || std::same_as<C, cancellation>,
                  "task only supports void or cancellation cancel channels");

    using value_type = T;
    using error_type = E;
    using cancel_type = C;

    using promise_type = task_promise_object<T, E>;

    task() noexcept = default;

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    task(task&& other) noexcept : h(std::exchange(other.h, nullptr)) {}

    task& operator=(task&& other) noexcept {
        if(this != &other) {
            destroy();
            h = std::exchange(other.h, nullptr);
        }
        return *this;
    }

    ~task() {
        destroy();
    }

    /// Awaits the task, which keeps its frame: that lives on until this task
    /// object goes, so the task can still be cancelled or asked while another
    /// awaits it.
    auto operator co_await() & noexcept {
        return awaiter<task&>{*this};
    }

    /// Awaits the task, whose frame goes once the await is over.
    auto operator co_await() && noexcept {
        return awaiter<task>{std::move(*this)};
    }

    /// Wrap this task so that co_await propagates errors directly to the parent
    /// without resuming the parent coroutine. On success, returns the unwrapped value.
    ///
    ///   auto val = co_await some_task().or_fail();
    ///
    auto or_fail() && noexcept
        requires (!std::is_void_v<E>) && std::is_void_v<C> {
        return detail::or_fail_proxy<task>{std::move(*this)};
    }

    /// The same task with a cancel channel: its cancellation becomes a value for
    /// the task awaiting it instead of cancelling that task too.
    task<T, E, cancellation> catch_cancel() && noexcept {
        return task<T, E, cancellation>(std::exchange(h, nullptr));
    }

    /// Cancels the task. One that has not started never runs; one that runs
    /// goes on to its next suspending co_await and ends there; one that is
    /// suspended passes the cancel on to what it awaits and ends once that has.
    /// It ends cancelled unless it fails first. A finished task stays as it is.
    void cancel() {
        h.promise().cancel();
    }

    /// Whether the task has ended, whichever way.
    bool done() const noexcept {
        return h.promise().done();
    }

    /// Whether the task ended cancelled.
    bool is_cancelled() const noexcept {
        return h.promise().state == async_node::State::Cancelled;
    }

    /// What the task ended with, as co_await gives it; rethrows what it threw.
    /// The task must have ended, and without a cancel channel not cancelled.
    auto result() {
        assert(done() && "result() of a task that has not ended");
        return narrow(h.promise().take());
    }

private:
    friend struct detail::task_access;
    template <typename, typename, typename>
    friend class task;
    template <typename, typename>
    friend struct task_return_object;

    using coroutine_handle = std::coroutine_handle<promise_type>;

    template <typename Awaitee>
    struct awaiter {
        Awaitee awaitee;

        bool await_ready() const noexcept {
            return false;
        }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<Promise> waiting,
            std::source_location location = std::source_location::current()) noexcept {
            return detail::task_access::await(waiting.promise(), awaitee, nullptr, location);
        }

        auto await_resume() {
            return narrow(awaitee.h.promise().take());
        }
    };

    explicit task(coroutine_handle h) noexcept : h(h) {}

    /// Narrows what a task ended with to what this task's channels carry.
    static auto narrow(outcome<T, E, cancellation>&& ended) {
        if constexpr(!std::is_void_v<C>) {
            return std::move(ended);
        } else {
            assert(!ended.is_cancelled() && "a task without a cancel channel ended cancelled");
            if constexpr(std::is_void_v<E>) {
                return std::move(ended).unwrap();
            } else {
                using narrowed = outcome<T, E>;
                if(ended.has_error()) {
                    return narrowed(outcome_error(std::move(ended).error()));
                }
                if constexpr(std::is_void_v<T>) {
                    return narrowed();
                } else {
                    return narrowed(std::move(*ended));
                }
            }
        }
    }

    void destroy() noexcept {
        if(h) {
            assert(h.promise().state != async_node::State::Running &&
                   "task destroyed while it runs");
            h.destroy();
        }
    }

    coroutine_handle h;
};

}  // namespace kota
