#pragma once

#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <set>
#include <source_location>
#include <string>
#include <vector>

namespace eventide {

/// Type-erased base for all coroutine-related nodes in the task tree.
///
/// Node hierarchy:
///   async_node
///     ├─ standard_task   — user coroutine (task<T>)
///     ├─ sync_primitive   — sync primitives (mutex, event, semaphore, cv)
///     ├─ waiter_link      — entry in a sync_primitive wait queue
///     ├─ aggregate_op     — when_all / when_any / async_scope
///     └─ system_op        — pending libuv I/O operation
class async_node {
public:
    enum class NodeKind : std::uint8_t {
        Task,

        /// Sync primitives — sync_primitive subclasses.
        Mutex,
        Event,
        Semaphore,
        ConditionVariable,

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
    };

    const NodeKind kind;

    Policy policy = None;

    State state = Pending;

    bool root = false;

    std::source_location location;

    bool is_standard_task() const noexcept {
        return kind == NodeKind::Task;
    }

    bool is_sync_primitive() const noexcept {
        return NodeKind::Mutex <= kind && kind <= NodeKind::ConditionVariable;
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

    /// If this node is a task, clear its awaitee pointer.
    void clear_awaitee() noexcept;

    void cancel();

    void resume();

    std::coroutine_handle<> link_continuation(async_node* awaiter, std::source_location location);

    std::coroutine_handle<> final_transition();

    std::coroutine_handle<> handle_subtask_result(async_node* parent);

    /// Dump the async node tree rooted at this node as a DOT (graphviz) graph.
    std::string dump_dot() const;

private:
    const static async_node* get_awaiter(const async_node* node);

    static void dump_dot_walk(const async_node* node,
                              std::set<const async_node*>& visited,
                              std::string& out);

protected:
    explicit async_node(NodeKind k) : kind(k) {}
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

class sync_primitive;

class waiter_link : public async_node {
public:
    friend class async_node;
    friend class sync_primitive;

    explicit waiter_link(NodeKind k) : async_node(k) {}

protected:
    /// The sync_primitive this waiter is queued on (nullptr if not queued).
    sync_primitive* resource = nullptr;

    /// Intrusive doubly-linked list pointers for the sync_primitive's wait queue.
    waiter_link* prev = nullptr;
    waiter_link* next = nullptr;

    /// The task that is suspended waiting for this waiter to be signalled.
    async_node* awaiter = nullptr;
};

class sync_primitive : public async_node {
public:
    friend class async_node;

    explicit sync_primitive(NodeKind k) : async_node(k) {}

    /// Appends a waiter to the end of the wait queue.
    void insert(waiter_link* link);

    /// Removes a waiter from the wait queue.
    void remove(waiter_link* link);

protected:
    bool has_waiters() const noexcept {
        return head != nullptr;
    }

    waiter_link* pop_waiter() noexcept {
        auto* link = head;
        if(link) {
            remove(link);
        }
        return link;
    }

    template <typename Fn>
    void drain_waiters(Fn&& fn) {
        auto* cur = head;
        while(cur) {
            auto* next = cur->next;
            remove(cur);
            fn(cur);
            cur = next;
        }
    }

    bool resume_waiter(waiter_link* link) noexcept {
        if(!link) {
            return false;
        }
        auto* awaiting = link->awaiter;
        link->awaiter = nullptr;
        if(!awaiting || awaiting->is_cancelled()) {
            return false;
        }
        awaiting->resume();
        return true;
    }

private:
    /// Head and tail of the intrusive doubly-linked waiter queue.
    waiter_link* head = nullptr;
    waiter_link* tail = nullptr;
};

/// Base for when_all / when_any / async_scope.
///
/// Uses a two-phase protocol in await_suspend:
///   1. Arming: link all children, then resume them. During this phase,
///      children that complete synchronously set pending_resume/pending_cancel
///      instead of directly resuming the awaiter (to avoid use-after-resume).
///   2. Post-arm: check pending flags and resume the awaiter if needed.
///
/// The `done` flag prevents double-processing: once set, further callbacks
/// from children are ignored (noop_coroutine).
class aggregate_op : public async_node {
protected:
    friend class async_node;

    explicit aggregate_op(NodeKind k) : async_node(k) {}

protected:
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

    /// Set once this aggregate has produced its final result;
    /// further child completions are ignored (return noop_coroutine).
    bool done = false;

    /// True while await_suspend is linking and resuming children.
    /// During arming, synchronous completions defer to pending flags
    /// instead of directly resuming the awaiter.
    bool arming = false;

    /// A child completed (or won the race) while arming was in progress.
    bool pending_resume = false;

    /// A child was cancelled while arming was in progress.
    bool pending_cancel = false;

    /// Common await_suspend logic for all aggregate operations.
    /// The caller must populate `awaitees` and set `total` before calling.
    /// `should_break` is called after each child resume to decide early exit.
    template <typename Promise, typename BreakPred>
    std::coroutine_handle<> arm_and_resume(std::coroutine_handle<Promise> awaiter_handle,
                                           std::source_location location,
                                           BreakPred should_break) noexcept {
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
                if(should_break()) {
                    break;
                }
            }
        }

        arming = false;
        if(pending_resume && awaiter) {
            assert(awaiter->is_standard_task() && "aggregate awaiter must be a task");
            awaiter->clear_awaitee();
            if(pending_cancel) {
                awaiter->state = Cancelled;
                return awaiter->final_transition();
            }
            return static_cast<standard_task*>(awaiter)->handle();
        }

        return std::noop_coroutine();
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
