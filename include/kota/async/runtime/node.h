#pragma once

#include <algorithm>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <limits>
#include <source_location>
#include <vector>

#include "kota/support/config.h"

namespace kota {

class sync_primitive;
class task_frame;

/// Type-erased base for all coroutine-related nodes in the task tree.
///
/// This hierarchy models awaitable runtime entities only.
/// Shared sync resources (mutex/event/semaphore/cv) live outside it and are
/// referenced by wait_node nodes while a task is blocked on them.
class async_node {
public:
    enum class NodeKind : std::uint8_t {
        Task,

        /// Wait queue entries — wait_node subclasses.
        /// Semaphore and CV reuse EventWaiter (identical cancel semantics).
        MutexWaiter,
        EventWaiter,

        /// Aggregate operations — when_all / when_any / task_group.
        WhenAll,
        WhenAny,
        TaskGroup,

        /// Pending libuv I/O — timers, signals, fs, network, etc.
        SystemIO,
    };

    enum Policy : uint8_t {
        None = 0,
        /// Reserved for future use.
        ExplicitCancel = 1 << 0,
        /// When set, cancellation of this node does NOT fail upward.
        /// The parent resumes normally and can inspect the cancelled state.
        /// Used by catch_cancel() and with_token().
        InterceptCancel = 1 << 1,
    };

    enum State : uint8_t {
        Pending,
        Running,
        Cancelled,
        Finished,
        Failed,
    };

    const NodeKind kind;

    Policy policy = None;

    State state = Pending;

    std::source_location location;

    bool is_task_frame() const noexcept {
        return kind == NodeKind::Task;
    }

    bool is_wait_node() const noexcept {
        return NodeKind::MutexWaiter <= kind && kind <= NodeKind::EventWaiter;
    }

    bool is_aggregate_op() const noexcept {
        return NodeKind::WhenAll <= kind && kind <= NodeKind::TaskGroup;
    }

    bool is_finished() const noexcept {
        return state == Finished;
    }

    bool is_cancelled() const noexcept {
        return state == Cancelled;
    }

    bool is_failed() const noexcept {
        return state == Failed;
    }

    // Keep this out-of-line. clang -O3 miscompiles direct promise policy writes in
    // coroutine return-object conversions, which can drop InterceptCancel. See also
    // https://github.com/llvm/llvm-project/issues/105595. Fixed in clang 21.
    void intercept_cancel() noexcept;

    void cancel();

    void resume();

    std::coroutine_handle<> attach(async_node& parent, std::source_location location);

    std::coroutine_handle<> finalize();

private:
    /// Cancellation-checkpoint path of attach(): the awaiting task was
    /// cancelled while it was executing, so instead of starting new work
    /// under it, finalize it at this suspension point.
    std::coroutine_handle<> attach_cancelled(task_frame& parent);

public:
    std::coroutine_handle<> on_child_complete(async_node& child);

    static void resume_and_drain(std::coroutine_handle<> handle);

protected:
    explicit async_node(NodeKind k) : kind(k) {}

public:
    std::exception_ptr propagated_exception;
};

class task_frame : public async_node {
protected:
    friend class async_node;

    explicit task_frame() : async_node(NodeKind::Task) {}

public:
    bool root = false;

    /// Optional hook invoked when a child task fails, allowing the parent to
    /// intercept the error before normal resumption. Used by or_fail_task_await
    /// to propagate errors directly without resuming the parent coroutine.
    using error_hook = std::coroutine_handle<> (*)(async_node& child, async_node& parent);

    std::coroutine_handle<> handle() {
        return std::coroutine_handle<>::from_address(address);
    }

    bool has_child() const noexcept {
        return child != nullptr;
    }

    void set_child(async_node* node) noexcept {
        child = node;
    }

    /// A task's child pointer distinguishes three situations:
    ///   - child == some node: suspended, awaiting that node;
    ///   - child == this (sentinel): the coroutine is executing on the stack
    ///     (or scheduled to); cancel() must not finalize it — the frame
    ///     observes `state == Cancelled` at its next suspension point;
    ///   - child == nullptr: idle — not executing and awaiting nothing.
    bool is_executing() const noexcept {
        return child == this;
    }

    void mark_executing() noexcept {
        child = this;
    }

    void set_error_hook(error_hook fn) noexcept {
        error_hook_fn = fn;
    }

    error_hook get_error_hook() const noexcept {
        return error_hook_fn;
    }

    void clear_error_hook() noexcept {
        error_hook_fn = nullptr;
    }

    const async_node* get_parent() const noexcept {
        return parent;
    }

    const async_node* get_child() const noexcept {
        return child;
    }

protected:
    /// Stores the raw address of the coroutine frame (handle).
    ///
    /// Theoretically, this is redundant because the promise object is embedded
    /// within the coroutine frame. However, deriving the frame address from `this`
    /// (via `from_promise`) requires knowing the concrete Promise type to account
    /// for the opaque compiler overhead (e.g., resume/destroy function pointers)
    /// located before the promise.
    ///
    /// Since this base class is type-erased, we cannot calculate that offset dynamically
    /// and must explicitly cache the handle address here (costing 1 pointer size).
    void* address = nullptr;

private:
    async_node* parent = nullptr;

    async_node* child = nullptr;

    error_hook error_hook_fn = nullptr;
};

class wait_node : public async_node {
public:
    friend class async_node;
    friend class sync_primitive;

    explicit wait_node(NodeKind k) : async_node(k) {}

    const async_node* get_parent() const noexcept {
        return parent;
    }

    const sync_primitive* get_resource() const noexcept {
        return resource;
    }

    const wait_node* get_next() const noexcept {
        return next;
    }

protected:
    using abandon_fn = void (*)(void*) noexcept;

    /// The sync_primitive this waiter is queued on (nullptr if not queued).
    sync_primitive* resource = nullptr;

    /// Intrusive doubly-linked list pointers for the sync_primitive's wait queue.
    wait_node* prev = nullptr;
    wait_node* next = nullptr;

    async_node* parent = nullptr;

    void* abandon_context = nullptr;

    abandon_fn abandon = nullptr;
};

/// Base for when_all / when_any / task_group.
///
/// The aggregate settles (delivers its outcome to the parent) exactly when
/// `pending` drops to zero. `pending` counts unfinished children plus one pin
/// for every stack frame that is still iterating over `children` (the arming
/// loop in await_suspend and every cancel cascade). Re-entrant child
/// completions during those loops therefore only decrement the counter; they
/// can never resume the parent from inside a loop that still touches this
/// object.
///
/// The outcome itself is not latched as a separate "what to deliver" value;
/// it is derived in settle() from `decision`, `state` and the attribution
/// indices, so the delivered outcome and its attribution cannot diverge.
class aggregate_op : public async_node {
protected:
    friend class async_node;

    explicit aggregate_op(NodeKind k) : async_node(k) {}

public:
    const async_node* get_parent() const noexcept {
        return parent;
    }

    const std::vector<async_node*>& get_children() const noexcept {
        return children;
    }

protected:
    /// The first event that picked this aggregate's outcome. Latched once,
    /// with one exception: a child error upgrades any earlier decision,
    /// because an error must never be dropped silently.
    ///
    /// An external cancel() is tracked separately as `state == Cancelled`
    /// (set by cancel() before it dispatches on the node kind). settle()
    /// treats it like a Cancel decision: it upgrades a plain Resume, but a
    /// child error still outranks it.
    enum class Decision : std::uint8_t {
        /// Undecided. A when_all whose children all succeed settles as
        /// success without ever recording a decision.
        None,

        /// Resume the parent normally (when_any winner; task_group child
        /// failure, which is reported via join() instead).
        Resume,

        /// A child's un-intercepted cancellation cancels the aggregate.
        Cancel,

        /// A child failed with a structured error or exception.
        Error,
    };

    /// Sentinel value for when_any: no winner yet.
    constexpr static std::size_t npos = (std::numeric_limits<std::size_t>::max)();

    async_node* parent = nullptr;

    std::vector<async_node*> children;

    /// Unfinished children plus active stack pins. Zero means "safe to settle".
    std::size_t pending = 0;

    /// Index of the first child to finish (when_any only).
    std::size_t winner = npos;

    /// Index of the first child to finish with a structured error or exception.
    std::size_t first_error_child = npos;

    /// Index of the first child to finish with cancellation.
    std::size_t first_cancel_child = npos;

    Decision decision = Decision::None;

    /// Set once the outcome has been delivered to the parent.
    bool settled = false;

    /// Number of tombstoned (null) slots in `children` left behind by eager
    /// child-frame reclamation. Used by task_group only: completed children
    /// that join() will never inspect again are destroyed on completion
    /// instead of accumulating until the group is destroyed.
    std::size_t reclaimed = 0;

    /// Keeps `pending` above zero while a loop over `children` is on the
    /// stack. The holder must call settle_if_idle() after the pin dies.
    struct pin {
        aggregate_op& op;

        explicit pin(aggregate_op& op) noexcept : op(op) {
            op.pending += 1;
        }

        ~pin() {
            op.pending -= 1;
        }
    };

    /// True once an outcome has been picked (or an external cancel arrived);
    /// implies the children cancel cascade has already been triggered.
    bool decided() const noexcept {
        return decision != Decision::None || state == Cancelled;
    }

    /// Latches `d` as the outcome and cancels the remaining children.
    ///
    /// The caller must guarantee `pending > 0` for the duration of the
    /// cascade — either by holding a pin, or by being the completion handler
    /// of a child that has not been counted off yet.
    void decide(Decision d) {
        decision = d;
        cancel_children();
    }

    /// Cancels every child that has not reached a terminal state yet.
    /// Idempotent: terminal children ignore cancel(). Null slots are
    /// tombstones of reclaimed task_group children.
    void cancel_children() {
        for(auto* child: children) {
            if(child) {
                child->cancel();
            }
        }
    }

    std::size_t find_child_index(const async_node& child) const {
        auto it = std::ranges::find(children, &child);
        assert(it != children.end() && "child not found in aggregate");
        if(it == children.end())
            std::abort();
        return static_cast<std::size_t>(it - children.begin());
    }

    /// Rethrows the propagated exception if one was captured from a failed child.
    void rethrow_if_propagated() {
#if KOTA_ENABLE_EXCEPTIONS
        if(propagated_exception) {
            std::rethrow_exception(propagated_exception);
        }
#endif
    }

    /// Settles if all children completed, no pins are held, and a parent is
    /// attached (a task_group settles only after join()). Returns the
    /// coroutine to resume, or noop.
    std::coroutine_handle<> settle_if_idle() noexcept;

    /// Delivers the final outcome to the parent. Runs exactly once.
    std::coroutine_handle<> settle() noexcept;

    std::coroutine_handle<> arm_and_resume(async_node& parent_node,
                                           std::source_location loc) noexcept {
        this->location = loc;

        assert(parent_node.is_task_frame() && "aggregate parent must be a task");
        // Re-awaiting would re-attach children whose frames already
        // completed (or are still running) — undefined behavior.
        assert(pending == 0 && !settled && "aggregate ops are single-use; do not re-await");

        // Cancellation checkpoint: don't start any children under a parent
        // that is already cancelled. The unstarted child tasks are destroyed
        // together with the parent frame that owns this aggregate.
        if(parent_node.state == Cancelled) {
            auto* p = static_cast<task_frame*>(&parent_node);
            p->set_child(nullptr);
            return p->finalize();
        }

        static_cast<task_frame*>(&parent_node)->set_child(this);

        parent = &parent_node;
        pending = children.size();
        winner = npos;
        first_error_child = npos;
        first_cancel_child = npos;
        decision = Decision::None;
        settled = false;
        propagated_exception = nullptr;
        state = Running;

        {
            pin held(*this);

            for(auto* child: children) {
                assert(child && "aggregate contains a null child");
                child->attach(*this, location);
            }

            for(auto* child: children) {
                if(decided()) {
                    // A synchronous completion already picked the outcome and
                    // cancelled the remaining children; resuming a cancelled
                    // child would deliver a second completion.
                    break;
                }
                child->resume();
            }
        }

        // If every child completed synchronously, resume the parent now via
        // symmetric transfer.
        return settle_if_idle();
    }
};

class io_op : public async_node {
protected:
    friend class async_node;

    using on_cancel = void (*)(io_op* self);

    explicit io_op(NodeKind k = NodeKind::SystemIO) : async_node(k) {}

    /// Callback invoked when this operation is cancelled (e.g. to close a uv handle).
    on_cancel action = nullptr;

    async_node* parent = nullptr;

public:
    void complete() noexcept;

    const async_node* get_parent() const noexcept {
        return parent;
    }
};

}  // namespace kota
