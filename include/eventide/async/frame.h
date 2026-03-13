#pragma once

#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <limits>
#include <set>
#include <source_location>
#include <string>
#include <vector>

namespace eventide {

class sync_primitive;

/// Type-erased base for all coroutine-related nodes in the task tree.
///
/// This hierarchy models awaitable runtime entities only.
/// Shared sync resources (mutex/event/semaphore/cv) live outside it and are
/// referenced by waiter_link nodes while a task is blocked on them.
class async_node {
public:
    enum class NodeKind : std::uint8_t {
        Task,

        /// Wait queue entries — waiter_link subclasses.
        /// Semaphore and CV reuse EventWaiter (identical cancel semantics).
        MutexWaiter,
        EventWaiter,

        /// Aggregate operations — when_all / when_any / async_scope.
        WhenAll,
        WhenAny,
        Scope,

        /// Pending libuv I/O — timers, signals, fs, network, etc.
        SystemIO,
    };

    enum Policy : uint8_t {
        None = 0,
        /// Reserved for future use.
        ExplicitCancel = 1 << 0,
        /// When set, cancellation of this node does NOT propagate upward.
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

    bool root = false;

    std::source_location location;

    bool is_standard_task() const noexcept {
        return kind == NodeKind::Task;
    }

    bool is_waiter_link() const noexcept {
        return NodeKind::MutexWaiter <= kind && kind <= NodeKind::EventWaiter;
    }

    bool is_aggregate_op() const noexcept {
        return NodeKind::WhenAll <= kind && kind <= NodeKind::Scope;
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

    /// If this node is a task, clear its awaitee pointer.
    void clear_awaitee() noexcept;

    void cancel();

    void resume();

    std::coroutine_handle<> link_continuation(async_node* awaiter, std::source_location location);

    std::coroutine_handle<> final_transition();

    std::coroutine_handle<> handle_subtask_result(async_node* parent);

    /// Dump the async graph reachable from this node as a DOT (graphviz) graph.
    std::string dump_dot() const;

private:
    const static async_node* get_awaiter(const async_node* node);
    const static sync_primitive* get_resource_parent(const async_node* node);

    static void dump_dot_walk(const async_node* node,
                              std::set<const void*>& visited,
                              std::string& out);
    static void dump_dot_walk(const sync_primitive* resource,
                              std::set<const void*>& visited,
                              std::string& out);

protected:
    explicit async_node(NodeKind k) : kind(k) {}

public:
    std::exception_ptr propagated_exception;
};

class standard_task : public async_node {
protected:
    friend class async_node;

    explicit standard_task() : async_node(NodeKind::Task) {}

public:
    std::coroutine_handle<> handle() {
        return std::coroutine_handle<>::from_address(address);
    }

    bool has_awaitee() const noexcept {
        return awaitee != nullptr;
    }

    void detach_as_root() noexcept {
        awaiter = nullptr;
        root = true;
    }

    void set_awaitee(async_node* node) noexcept {
        awaitee = node;
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
    /// The node that awaits this task currently, if it is empty,
    /// this task is a top level task. It was launched by eventloop.
    async_node* awaiter = nullptr;

    /// The node that this task awaits.
    async_node* awaitee = nullptr;
};

class waiter_link : public async_node {
public:
    friend class async_node;
    friend class sync_primitive;

    explicit waiter_link(NodeKind k) : async_node(k) {}

protected:
    /// The sync_primitive this waiter is queued on (nullptr if not queued).
    sync_primitive* resource = nullptr;

    /// Captures which wait-queue generation this waiter joined.
    ///
    /// `event::interrupt()` must only cancel the waiters that were already
    /// present when the interrupt began. The tricky part is that cancelling one
    /// waiter resumes arbitrary user code synchronously, and that code may
    /// immediately enqueue a fresh waiter on the same resource before
    /// interrupt() continues. Tagging each waiter with the generation observed
    /// at insertion time lets interrupt() stop once it reaches a waiter that
    /// was added by a later, re-entrant wait.
    std::size_t generation = 0;

    /// Intrusive doubly-linked list pointers for the sync_primitive's wait queue.
    waiter_link* prev = nullptr;
    waiter_link* next = nullptr;

    /// The task that is suspended waiting for this waiter to be signalled.
    async_node* awaiter = nullptr;
};

/// Base for when_all / when_any / async_scope.
///
/// Uses a two-phase protocol in await_suspend:
///   1. Arming: link all children, then resume them. During this phase,
///      synchronous child completions are deferred instead of directly
///      resuming the awaiter (to avoid use-after-resume).
///   2. Post-arm: deliver any deferred completion once it is safe.
///
/// Aggregate state is tracked explicitly:
///   - `phase` controls whether child callbacks must be deferred.
///   - `deferred` latches the completion to deliver once deferral ends.
class aggregate_op : public async_node {
protected:
    friend class async_node;

    explicit aggregate_op(NodeKind k) : async_node(k) {}

protected:
    enum class Phase : std::uint8_t {
        /// Normal operating state after arming completes.
        /// Child callbacks may settle the aggregate immediately.
        Open,

        /// await_suspend is still linking/resuming children.
        /// Any child completion observed here must be deferred until
        /// await_suspend returns to avoid resuming the awaiter re-entrantly.
        Arming,

        /// The aggregate itself is propagating cancellation to children.
        /// Child callbacks can re-enter while this walk is in progress, so
        /// completion is deferred until the cancel cascade finishes.
        Cancelling,

        /// Final outcome has been chosen and further child callbacks are ignored.
        Settled,
    };

    enum class Deferred : std::uint8_t {
        /// No deferred completion is waiting to be delivered.
        None,

        /// Resume the awaiter normally.
        Resume,

        /// Resume the awaiter by propagating cancellation upward.
        Cancel,

        /// Resume the awaiter by propagating failure upward.
        Error,
    };

    /// Sentinel value for when_any: no winner yet.
    constexpr static std::size_t npos = (std::numeric_limits<std::size_t>::max)();

    /// The parent node that co_awaited this aggregate.
    async_node* awaiter = nullptr;

    /// Child nodes managed by this aggregate (tasks spawned into it).
    std::vector<async_node*> awaitees;

    /// Number of children that have completed so far.
    std::size_t completed = 0;

    /// Total number of children expected to complete.
    std::size_t total = 0;

    /// Index of the first child to finish (when_any only).
    std::size_t winner = npos;

    /// Runtime phase for this aggregate while it is awaiting children.
    Phase phase = Phase::Open;

    /// Completion latched while callbacks are being deferred.
    Deferred deferred = Deferred::None;

    bool is_settled() const noexcept {
        return phase == Phase::Settled;
    }

    bool is_deferring() const noexcept {
        return phase == Phase::Arming || phase == Phase::Cancelling;
    }

    /// Latch a normal completion, but preserve any stronger deferred signal
    /// (cancel/error) that may already have won.
    void defer_resume() noexcept {
        if(deferred == Deferred::None) {
            deferred = Deferred::Resume;
        }
    }

    /// Cancellation outranks a plain resume and is itself outranked only by error.
    void defer_cancel() noexcept {
        if(deferred != Deferred::Error) {
            deferred = Deferred::Cancel;
        }
    }

    /// Error outranks all other deferred outcomes.
    void defer_error() noexcept {
        deferred = Deferred::Error;
    }

    /// Rethrows the propagated exception if one was captured from a failed child.
    void rethrow_if_propagated() {
#ifdef __cpp_exceptions
        if(propagated_exception) {
            std::rethrow_exception(propagated_exception);
        }
#endif
    }

    /// Deliver the latched completion to the aggregate awaiter once it is safe
    /// to resume/propagate out of the current callback stack.
    std::coroutine_handle<> deliver_deferred() noexcept;

    /// Common await_suspend logic for all aggregate operations.
    /// The caller must populate `awaitees` and set `total` before calling.
    template <typename Promise>
    std::coroutine_handle<> arm_and_resume(std::coroutine_handle<Promise> awaiter_handle,
                                           std::source_location location) noexcept {
        this->location = location;

        auto* awaiter_node = static_cast<async_node*>(&awaiter_handle.promise());
        if(awaiter_node->kind == async_node::NodeKind::Task) {
            static_cast<standard_task*>(awaiter_node)->set_awaitee(this);
        }

        awaiter = awaiter_node;
        completed = 0;
        winner = npos;
        phase = Phase::Arming;
        deferred = Deferred::None;
        propagated_exception = nullptr;

        for(auto* child: awaitees) {
            if(child) {
                child->link_continuation(this, location);
            }
        }

        for(auto* child: awaitees) {
            if(child) {
                child->resume();
                if(is_settled() || deferred != Deferred::None) {
                    break;
                }
            }
        }

        if(phase == Phase::Arming) {
            phase = Phase::Open;
        }

        return deferred != Deferred::None ? deliver_deferred() : std::noop_coroutine();
    }
};

class system_op : public async_node {
protected:
    friend class async_node;

    using on_cancel = void (*)(system_op* self);

    explicit system_op(NodeKind k = NodeKind::SystemIO) : async_node(k) {}

    /// Callback invoked when this operation is cancelled (e.g. to close a uv handle).
    on_cancel action = nullptr;

    /// The parent node that is waiting for this I/O operation to complete.
    async_node* awaiter = nullptr;

public:
    void complete() noexcept;
};

}  // namespace eventide
