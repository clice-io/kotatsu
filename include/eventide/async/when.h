#pragma once

#include <cstddef>
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
    return std::move(task).result();
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
        release_inflight(std::index_sequence_for<Tasks...>{});
    }

    bool await_ready() const noexcept {
        return sizeof...(Tasks) == 0;
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> awaiter_handle,
                      std::source_location location = std::source_location::current()) noexcept {
        this->location = location;

        auto* awaiter_node = static_cast<async_node*>(&awaiter_handle.promise());
        if(awaiter_node->kind == async_node::NodeKind::Task) {
            static_cast<standard_task*>(awaiter_node)->set_awaitee(this);
        }

        awaiter = awaiter_node;
        completed = 0;
        total = sizeof...(Tasks);
        winner = npos;
        done = false;
        pending_resume = false;
        pending_cancel = false;
        arming = true;

        awaitees.clear();
        awaitees.reserve(total);
        add_awaitees(std::index_sequence_for<Tasks...>{});

        for(auto* child: awaitees) {
            if(child) {
                child->link_continuation(this, location);
            }
        }

        for(auto* child: awaitees) {
            if(child) {
                child->resume();
                if(pending_cancel) {
                    break;
                }
            }
        }

        arming = false;
        if(pending_resume && awaiter) {
            awaiter->clear_awaitee();
            if(pending_cancel) {
                awaiter->state = Cancelled;
                return awaiter->final_transition();
            }

            return static_cast<standard_task*>(awaiter)->handle();
        }

        return std::noop_coroutine();
    }

    auto await_resume() {
        return collect(std::index_sequence_for<Tasks...>{});
    }

private:
    template <std::size_t... I>
    void release_inflight(std::index_sequence<I...>) noexcept {
        (detail::release_inflight(std::get<I>(tasks_)), ...);
    }

    template <std::size_t... I>
    void add_awaitees(std::index_sequence<I...>) {
        (awaitees.push_back(detail::node_from(std::get<I>(tasks_))), ...);
    }

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
        release_inflight(std::index_sequence_for<Tasks...>{});
    }

    bool await_ready() const noexcept {
        return sizeof...(Tasks) == 0;
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> awaiter_handle,
                      std::source_location location = std::source_location::current()) noexcept {
        this->location = location;

        auto* awaiter_node = static_cast<async_node*>(&awaiter_handle.promise());
        if(awaiter_node->kind == async_node::NodeKind::Task) {
            static_cast<standard_task*>(awaiter_node)->set_awaitee(this);
        }

        awaiter = awaiter_node;
        completed = 0;
        total = sizeof...(Tasks);
        winner = npos;
        done = false;
        pending_resume = false;
        pending_cancel = false;
        arming = true;

        awaitees.clear();
        awaitees.reserve(total);
        add_awaitees(std::index_sequence_for<Tasks...>{});

        for(auto* child: awaitees) {
            if(child) {
                child->link_continuation(this, location);
            }
        }

        for(auto* child: awaitees) {
            if(child) {
                child->resume();
                if(done || pending_resume || pending_cancel) {
                    break;
                }
            }
        }

        arming = false;
        if(pending_resume && awaiter) {
            awaiter->clear_awaitee();
            if(pending_cancel) {
                awaiter->state = Cancelled;
                return awaiter->final_transition();
            }

            return static_cast<standard_task*>(awaiter)->handle();
        }

        return std::noop_coroutine();
    }

    std::size_t await_resume() const noexcept {
        return winner;
    }

private:
    template <std::size_t... I>
    void release_inflight(std::index_sequence<I...>) noexcept {
        (detail::release_inflight(std::get<I>(tasks_)), ...);
    }

    template <std::size_t... I>
    void add_awaitees(std::index_sequence<I...>) {
        (awaitees.push_back(detail::node_from(std::get<I>(tasks_))), ...);
    }

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
        this->location = location;

        auto* awaiter_node = static_cast<async_node*>(&awaiter_handle.promise());
        if(awaiter_node->kind == async_node::NodeKind::Task) {
            static_cast<standard_task*>(awaiter_node)->set_awaitee(this);
        }

        awaiter = awaiter_node;
        completed = 0;
        winner = npos;
        done = false;
        pending_resume = false;
        pending_cancel = false;
        arming = true;

        for(auto* child: awaitees) {
            if(child) {
                child->link_continuation(this, location);
            }
        }

        for(auto* child: awaitees) {
            if(child) {
                child->resume();
                if(pending_cancel) {
                    break;
                }
            }
        }

        arming = false;
        if(pending_resume && awaiter) {
            awaiter->clear_awaitee();
            if(pending_cancel) {
                awaiter->state = Cancelled;
                return awaiter->final_transition();
            }

            return static_cast<standard_task*>(awaiter)->handle();
        }

        return std::noop_coroutine();
    }

    void await_resume() noexcept {}
};

}  // namespace eventide
