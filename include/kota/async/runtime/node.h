#pragma once

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <source_location>
#include <span>

#include "kota/support/config.h"

namespace kota {

class task_frame;
class aggregate_op;
class wait_node;
class io_op;
class event_loop;

template <typename T, typename E, typename C>
class task;

template <typename Derived>
class async_visitor;

namespace detail {

struct task_access;

}

/// Base of the nodes of the task tree: tasks, the aggregates when_all,
/// when_any and task_group, waits on sync primitives, and pending I/O. Every
/// node points at the node awaiting it; a suspended task points at the node it
/// awaits, and an aggregate lists its running children.
class async_node {
public:
    enum class NodeKind : std::uint8_t {
        Task,
        /// A task's wait on a mutex, semaphore, event or condition variable.
        Waiter,
        WhenAll,
        WhenAny,
        TaskGroup,
        /// Pending I/O: timers, signals, fs, network, ...
        SystemIO,
    };

    /// Where a node is in its life; the last three are final.
    enum class State : std::uint8_t {
        /// Not started yet.
        Pending,
        /// Started and not finished.
        Running,
        Succeeded,
        /// Ended with an error or an exception.
        Failed,
        Cancelled,
    };

    const NodeKind kind;

protected:
    friend class task_frame;
    friend class aggregate_op;
    friend class wait_node;
    friend class io_op;
    template <typename Derived>
    friend class async_visitor;

    explicit async_node(NodeKind kind) noexcept : kind(kind) {}

    bool done() const noexcept {
        return state >= State::Succeeded;
    }

    /// Cancels this node and what it waits on; later calls do nothing. A task
    /// that has not started only records it: it never runs. A running task
    /// records it and ends at its next suspending co_await. A suspended one
    /// passes it on to what it awaits and ends once that has.
    void cancel();

    /// Records that `waiting` awaits this node, from `location`.
    void awaited_by(task_frame& waiting, std::source_location location) noexcept;

    /// Delivers the completion of `child`, which this node awaits; returns the
    /// coroutine to resume next.
    std::coroutine_handle<> on_child_complete(async_node& child);

    /// Resumes `handle`, then, unless it runs inside another resumption, the
    /// tasks sync primitives woke meanwhile.
    static void resume_and_drain(std::coroutine_handle<> handle);

    State state = State::Pending;

    /// cancel() has reached this node.
    bool cancel_requested = false;

    /// This node ending cancelled resumes its parent, which sees the
    /// cancellation as a value, instead of cancelling the parent too.
    bool intercept = false;

    /// The node awaiting this one: null before it starts, once it has
    /// completed, and for a root task.
    async_node* parent = nullptr;

    /// Where this node was awaited, spawned or scheduled.
    std::source_location location;
};

/// The part of a task's promise that does not depend on its result type: the
/// task's place in the tree and its life from start to end.
class task_frame : public async_node {
protected:
    friend class async_node;
    friend class aggregate_op;
    friend class wait_node;
    friend class io_op;
    friend struct detail::task_access;
    template <typename T, typename E, typename C>
    friend class task;
    template <typename Derived>
    friend class async_visitor;

    /// Moves the error of `child`, a task that failed without throwing, into
    /// `parent`: the task awaiting it through or_fail(), or its aggregate.
    using error_hook = void (*)(task_frame& child, async_node& parent);

    task_frame() noexcept : async_node(NodeKind::Task) {}

    std::coroutine_handle<> handle() const noexcept {
        return std::coroutine_handle<>::from_address(address);
    }

    bool threw() const noexcept {
#if KOTA_ENABLE_EXCEPTIONS
        return exception != nullptr;
#else
        return false;
#endif
    }

    /// Starts this task: under `parent`, or as a root when that is null. A task
    /// cancelled before it starts never runs; it ends cancelled at once.
    /// Returns the coroutine to resume next: this task, or what its end
    /// resumes.
    std::coroutine_handle<> start(async_node* parent);

    /// The cancellation checkpoint of every suspending co_await: a cancel that
    /// reached this task while it ran ends it here, before it starts new work.
    /// Returns what to resume instead, or null when no cancel is pending.
    std::coroutine_handle<> checkpoint();

    /// Starts `child` as the task this one awaits; returns the coroutine to
    /// resume next.
    std::coroutine_handle<> await_task(task_frame& child,
                                       bool intercept,
                                       error_hook hook,
                                       std::source_location location);

    /// Ends this task in `end` and delivers that to its parent; a root the
    /// event loop owns is destroyed. Returns the coroutine to resume next.
    std::coroutine_handle<> finish(State end);

    /// The coroutine frame. The promise knows its own handle, but this
    /// type-erased base cannot derive the frame address from `this`.
    void* address = nullptr;

    /// What this task awaits while it is suspended; null while it runs.
    async_node* child = nullptr;

    /// Links in the list of running children of the aggregate this task is a
    /// child of.
    task_frame* prev_sibling = nullptr;
    task_frame* next_sibling = nullptr;

    error_hook hook = nullptr;

    /// The task was scheduled on an event loop, which starts it as a root.
    bool scheduled = false;

    /// The event loop owns the frame and destroys it once the task ends.
    bool owned_by_loop = false;

#if KOTA_ENABLE_EXCEPTIONS
    std::exception_ptr exception;
#endif
};

/// Base of when_all, when_any and task_group. An aggregate settles, which
/// delivers its outcome to the task awaiting it, once `pending` drops to zero.
/// `pending` counts the children that have not finished plus one pin for every
/// stack frame still walking the children, so a child that finishes inside
/// such a walk never settles the aggregate under it. What the aggregate
/// settles as follows from what happened, in this order: a child failed, it
/// was cancelled (by a when_all or when_any child, or from outside), or it
/// succeeded.
class aggregate_op : public async_node {
protected:
    friend class async_node;
    template <typename Derived>
    friend class async_visitor;

    using error_hook = task_frame::error_hook;

    /// What settles the aggregate. The first decision counts, except that an
    /// error replaces any other: errors are never dropped.
    enum class Decision : std::uint8_t {
        None,
        /// Resume the awaiting task: a when_any child won, or
        /// task_group::cancel() was called.
        Resume,
        /// A when_all or when_any child was cancelled.
        Cancel,
        /// A child failed.
        Error,
    };

    /// Keeps `pending` above zero while a walk over the children is on the
    /// stack. The holder settles once the pin is gone.
    struct pin {
        aggregate_op& op;

        explicit pin(aggregate_op& op) noexcept : op(op) {
            op.pending += 1;
        }

        pin(const pin&) = delete;
        pin& operator=(const pin&) = delete;

        ~pin() {
            op.pending -= 1;
        }
    };

    explicit aggregate_op(NodeKind kind) noexcept : async_node(kind) {}

    /// An outcome has been picked, or a cancel came from outside; either way
    /// the children have been cancelled.
    bool decided() const noexcept {
        return decision != Decision::None || cancel_requested;
    }

    /// Sets the hook `child` reports its error through, if it fails.
    static task_frame& watch(task_frame& child, error_hook hook) noexcept;

    /// when_all and when_any: starts `children` under this aggregate, which
    /// `waiting` awaits. Under a cancelled task none of them starts. Returns
    /// the coroutine to resume next.
    std::coroutine_handle<> arm(task_frame& waiting,
                                std::span<task_frame* const> children,
                                std::source_location location);

    /// task_group: starts `child` and runs it until it first suspends.
    void spawn(task_frame& child, std::source_location location);

    /// task_group: makes `waiting` await this group until every child has
    /// ended. Returns the coroutine to resume next.
    std::coroutine_handle<> await_children(task_frame& waiting, std::source_location location);

    /// ~task_group: lets every running child go. Each is cancelled, ends on
    /// its own, and is freed by the event loop then; what it failed with is
    /// dropped. A child may end, or run on to its next suspending co_await,
    /// before this returns; the group refuses to spawn meanwhile.
    void abandon_children();

    /// Records the completion of `child` and settles when it was the last.
    std::coroutine_handle<> child_completed(task_frame& child);

    /// Records `d` as what settles the aggregate; the first decision cancels
    /// the children still running.
    void decide(Decision d);

    /// Cancels every running child.
    void cancel_children();

    /// A cancel from outside: cancels every child, then settles if they have
    /// all finished. Returns the coroutine to resume next.
    std::coroutine_handle<> cancel_all();

    /// What the aggregate settles as.
    State settled_state() const noexcept;

    /// Settles when every child has finished and no pin is held, if a task
    /// awaits the aggregate. Returns the coroutine to resume next.
    std::coroutine_handle<> settle_if_idle();

    /// The running children, in the order they started.
    task_frame* head = nullptr;
    task_frame* tail = nullptr;

    std::size_t pending = 0;

    /// when_any: the child that finished first.
    task_frame* winner = nullptr;

    Decision decision = Decision::None;

#if KOTA_ENABLE_EXCEPTIONS
    /// The first exception a child threw. It outranks any error.
    std::exception_ptr exception;
#endif

private:
    void link(task_frame& child) noexcept;
    void unlink(task_frame& child) noexcept;
};

/// Base of a pending I/O operation, the extension point for awaiting what
/// completes through a callback. Set `action` to what cancels the operation,
/// call attach() from await_suspend and complete() once the operation is
/// over, whether it succeeded, failed or was cancelled.
class io_op : public async_node {
public:
    /// Ends the operation and resumes the task awaiting it. An operation that
    /// cancel() reached stays cancelled, whatever it finished with.
    void complete() noexcept;

    /// Like complete(), once whatever runs now has suspended, as a sync
    /// primitive resumes the tasks it grants: for an operation that ends
    /// inside another task, which must not resume the task awaiting it in
    /// the middle. `loop` is the awaiting task's.
    void complete_deferred(event_loop& loop) noexcept;

    /// Whether cancel() has reached the operation.
    bool cancel_requested() const noexcept {
        return async_node::cancel_requested;
    }

protected:
    friend class async_node;

    using on_cancel = void (*)(io_op* self);

    io_op() noexcept : async_node(NodeKind::SystemIO) {}

    /// Makes `waiting` await this operation. Under a cancelled task the
    /// operation, which may already be in flight, is cancelled at once; its
    /// completion then ends the task, which may destroy this operation, so
    /// await_suspend must touch nothing after.
    std::coroutine_handle<> attach(task_frame& waiting, std::source_location location) noexcept;

    /// Cancels the operation. cancel() marks the operation cancelled before it
    /// calls this; the operation still has to complete().
    on_cancel action = nullptr;
};

}  // namespace kota
