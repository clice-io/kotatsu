#include "kota/async/runtime/node.h"

#include <cassert>
#include <utility>
#include <vector>

#include "../libuv.h"
#include "kota/async/io/loop.h"
#include "kota/async/runtime/sync.h"

namespace kota {

namespace {

#if KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
thread_local std::vector<std::coroutine_handle<>> pending_frame_destroys;
#endif

#if KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
void drain_pending_destroys() {
    while(!pending_frame_destroys.empty()) {
        auto queued = std::move(pending_frame_destroys);
        pending_frame_destroys.clear();
        for(auto handle: queued) {
            assert(handle && "pending destroy queue contains a null handle");
            handle.destroy();
        }
    }
}
#endif

}  // namespace

void async_node::resume_and_drain(std::coroutine_handle<> handle) {
    static thread_local bool draining = false;

    assert(handle && "resume_and_drain called with null handle");
    const bool outermost = !draining;
    if(outermost) {
        draining = true;
    }

    handle.resume();
#if KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    drain_pending_destroys();
#endif

    if(outermost && event_loop::has_current()) {
        event_loop::current().drain_deferred();
    }

    if(outermost) {
        draining = false;
    }
}

void async_node::intercept_cancel() noexcept {
    policy = static_cast<Policy>(policy | InterceptCancel);
}

std::coroutine_handle<> aggregate_op::settle_if_idle() noexcept {
    if(pending != 0 || settled || !parent) {
        return std::noop_coroutine();
    }
    return settle();
}

/// Outcome precedence, derived from the recorded facts (errors are never
/// dropped, even when an external cancel() raced the failing child — the
/// same rule trio and Kotlin coroutines use for real errors vs cancellation):
///   1. a child error;
///   2. any cancellation — resumed with the cancel outcome when
///      intercepting, propagated upward via finalize otherwise;
///   3. success.
std::coroutine_handle<> aggregate_op::settle() noexcept {
    assert(pending == 0 && parent && !settled && "settle() caller must check settle_if_idle");
    settled = true;

    assert(parent->is_task_frame() && "aggregate parent must be a task");
    auto* p = static_cast<task_frame*>(parent);
    parent = nullptr;

    const bool intercepts = policy & InterceptCancel;
    const bool external_cancel = state == Cancelled;

    if(decision == Decision::Error) {
        state = Failed;
        p->mark_executing();
        return p->handle();
    }

    if(external_cancel || decision == Decision::Cancel) {
        state = Cancelled;
        if(intercepts) {
            p->mark_executing();
            return p->handle();
        }
        p->set_child(nullptr);
        p->state = Cancelled;
        return p->finalize();
    }

    state = Finished;
    p->mark_executing();
    return p->handle();
}

/// Recursively cancels this node and all of its descendants.
/// Idempotent: re-cancelling an already-terminal node is a no-op.
void async_node::cancel() {
    if(state == Cancelled || state == Failed || state == Finished) {
        return;
    }
    state = Cancelled;

    switch(kind) {
        case NodeKind::Task: {
            auto* self = static_cast<task_frame*>(this);
            if(self->is_executing()) {
                // The frame is live on the stack (or scheduled); it observes
                // state == Cancelled at its next suspension point.
                break;
            }
            if(self->child) {
                self->child->cancel();
            } else if(self->parent) {
                auto next = self->finalize();
                async_node::resume_and_drain(next);
            }
            break;
        }
        case NodeKind::MutexWaiter:
        case NodeKind::EventWaiter: {
            auto* self = static_cast<wait_node*>(this);
            if(auto* res = self->resource) {
                res->remove(self);
            }
            auto* p = self->parent;
            self->parent = nullptr;
            assert(p && "wait_node cancelled without a parent");
            auto next = p->on_child_complete(*self);
            async_node::resume_and_drain(next);
            break;
        }

        case NodeKind::WhenAll:
        case NodeKind::WhenAny:
        case NodeKind::TaskGroup: {
            auto* self = static_cast<aggregate_op*>(this);

            // state == Cancelled (set above) is the record of this external
            // cancel; settle() derives the outcome from it. The cascade can
            // complete every remaining child synchronously, so hold a pin
            // while it runs and settle only afterwards.
            {
                aggregate_op::pin held(*self);
                self->cancel_children();
            }

            auto next = self->settle_if_idle();
            async_node::resume_and_drain(next);
            break;
        }

        case NodeKind::SystemIO: {
            auto* self = static_cast<io_op*>(this);
            assert(self->action && "io_op cancelled without a cancellation action");
            self->action(self);
            break;
        }
    }
}

/// Resumes a task's coroutine, or finalizes it if already cancelled/failed.
///
/// The cancel/fail path handles the case where a sync primitive deferred
/// this resume and cancellation arrived before the deferred tick fired.
/// If the child wait_node was resolved by resume_waiter (state == Finished),
/// its grant (e.g. a mutex lock) is abandoned before finalization.
void async_node::resume() {
    if(is_task_frame()) {
        auto* f = static_cast<task_frame*>(this);
        if(is_cancelled() || is_failed()) {
            auto* ch = f->child;
            if(ch && ch->is_wait_node() && ch->is_finished()) {
                auto* wn = static_cast<wait_node*>(ch);
                if(wn->abandon) {
                    auto fn = wn->abandon;
                    auto ctx = wn->abandon_context;
                    wn->abandon = nullptr;
                    wn->abandon_context = nullptr;
                    fn(ctx);
                }
            }
            f->set_child(nullptr);
            // finalize() handles every ownership case: parent attached →
            // deliver the completion; detached root → destroy the frame
            // (a pre-cancelled scheduled task would otherwise leak it);
            // otherwise noop and the owning task object reclaims it.
            resume_and_drain(f->finalize());
            return;
        }
        f->mark_executing();
        f->handle().resume();
#if KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
        drain_pending_destroys();
#endif
    }
}

/// Called by libuv callbacks when an I/O operation completes.
/// Preserves Cancelled state if already set, then notifies the parent.
void io_op::complete() noexcept {
    if(state != Cancelled) {
        state = Finished;
    }
    auto* p = parent;
    parent = nullptr;
    assert(p && "io_op completed without a linked parent");
    auto next = p->on_child_complete(*this);
    async_node::resume_and_drain(next);
}

/// Cancellation checkpoint: the parent observes its cancellation at the next
/// suspending co_await instead of starting new work (trio-style prompt
/// cancellation). Everything below only returns coroutine handles — nothing
/// is resumed while the parent's await_suspend is still on the stack; the
/// compiler performs the transfer after it returns.
std::coroutine_handle<> async_node::attach_cancelled(task_frame& parent) {
    assert(parent.state == Cancelled && "checkpoint requires a cancelled parent");

    switch(kind) {
        case NodeKind::Task:
            // Never started; the frame is destroyed together with the parent
            // frame that owns the task object.
            parent.set_child(nullptr);
            return parent.finalize();

        case NodeKind::MutexWaiter:
        case NodeKind::EventWaiter: {
            auto* self = static_cast<wait_node*>(this);
            if(auto* res = self->resource) {
                res->remove(self);
            }
            parent.set_child(nullptr);
            return parent.finalize();
        }

        case NodeKind::WhenAll:
        case NodeKind::WhenAny:
        case NodeKind::TaskGroup:
            // Aggregates run their checkpoints in arm_and_resume / join.
            break;

        case NodeKind::SystemIO: {
            // The uv request may already be in flight, so it cannot be
            // abandoned synchronously: link the parent, then cancel the
            // operation. Its completion — synchronous or via a later uv
            // callback — propagates through on_child_complete and finalizes
            // the parent with structured completion intact. cancel() may
            // destroy this node and the parent frame; touch nothing after.
            auto* self = static_cast<io_op*>(this);
            self->parent = &parent;
            parent.set_child(this);
            this->cancel();
            return std::noop_coroutine();
        }
    }

    std::abort();
}

/// Wires this node as a child of `parent_node`. For Task nodes, sets state
/// to Running and returns the coroutine handle (ready to resume).
/// For transient nodes (wait_node, io_op), records the parent
/// and returns noop_coroutine (resumed later by event/complete).
std::coroutine_handle<> async_node::attach(async_node& parent_node, std::source_location loc) {
    this->location = loc;

    if(parent_node.is_task_frame() && parent_node.state == Cancelled) {
        return attach_cancelled(static_cast<task_frame&>(parent_node));
    }

    if(parent_node.kind == NodeKind::Task) {
        static_cast<task_frame*>(&parent_node)->child = this;
    }

    switch(this->kind) {
        case NodeKind::Task: {
            auto self = static_cast<task_frame*>(this);
            if(self->state == Cancelled) {
                // Cancelled before it ever started: never run the body,
                // deliver the completion to the parent right away. The
                // parent link stays null — the completion is already
                // delivered here, and resume()/cancel() recognize a
                // completed task by that.
                return parent_node.on_child_complete(*self);
            }
            self->state = Running;
            self->parent = &parent_node;
            return self->handle();
        }

        case NodeKind::MutexWaiter:
        case NodeKind::EventWaiter: {
            auto self = static_cast<wait_node*>(this);
            self->parent = &parent_node;
            return std::noop_coroutine();
        }
        case NodeKind::WhenAll:
        case NodeKind::WhenAny:
        case NodeKind::TaskGroup: break;
        case NodeKind::SystemIO: {
            auto self = static_cast<io_op*>(this);
            self->parent = &parent_node;
            return std::noop_coroutine();
        }
    }

    std::abort();
}

/// Called when a task reaches final_suspend (Finished, Cancelled, or Failed).
/// For root tasks with no parent, destroys the coroutine frame.
/// Otherwise, notifies the parent via on_child_complete.
std::coroutine_handle<> async_node::finalize() {
    switch(kind) {
        case NodeKind::Task: {
            auto self = static_cast<task_frame*>(this);
            if(!self->parent) {
                if(self->root) {
#if KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
                    pending_frame_destroys.push_back(self->handle());
#else
                    self->handle().destroy();
#endif
                }
                return std::noop_coroutine();
            }

            // Sever the parent link before delivering the completion: a
            // completion is delivered at most once, and resume()/cancel()
            // recognize an already-finalized task by its null parent.
            auto* p = self->parent;
            self->parent = nullptr;
            return p->on_child_complete(*self);
        }

        case NodeKind::MutexWaiter:
        case NodeKind::EventWaiter:
        case NodeKind::WhenAll:
        case NodeKind::WhenAny:
        case NodeKind::TaskGroup:
        case NodeKind::SystemIO: break;
    }

    std::abort();
}

/// Dispatches a child's completion to its parent node.
///
/// For Task parents: resumes the coroutine normally for Finished/Failed,
///   or propagates cancellation upward.
/// For Aggregate parents (when_all/when_any/task_group): records the event
///   (error / cancellation / winner), cancels the remaining children on the
///   first deciding event, counts the child off, and settles once every
///   child has completed (structured completion).
std::coroutine_handle<> async_node::on_child_complete(async_node& child) {
    assert(&child != this && "invalid parameter!");

    switch(kind) {
        case NodeKind::Task: {
            auto self = static_cast<task_frame*>(this);

            if(child.state == Cancelled) {
                if(child.policy & InterceptCancel) {
                    self->mark_executing();
                    return self->handle();
                }

                self->set_child(nullptr);
                self->state = Cancelled;
                return self->finalize();
            }

            self->mark_executing();
            if(child.state == Failed && child.kind == NodeKind::Task) {
                auto* child_task = static_cast<task_frame*>(&child);
                if(auto propagate = child_task->get_error_hook()) {
                    return propagate(child, *self);
                }
            }
            return self->handle();
        }

        case NodeKind::WhenAll:
        case NodeKind::WhenAny: {
            auto self = static_cast<aggregate_op*>(this);
            assert(!self->settled && "aggregate received child completion after settling");
            assert(self->pending > 0 && "aggregate completed more children than it owns");

            const bool intercepts = self->policy & InterceptCancel;
            // A cancelled child is always a cancellation event: a child
            // carries InterceptCancel iff its task type has a cancel
            // channel, and any such child forces the aggregate to
            // intercept as well (see the when_op constructor).
            const bool cancelled = child.state == Cancelled;

            // The completing child is not counted off until below, so the
            // cancel cascades inside decide() cannot settle re-entrantly.
            if(child.state == Failed) {
                if(self->first_error_child == aggregate_op::npos) {
                    self->first_error_child = self->find_child_index(child);
                    if(child.propagated_exception) {
                        self->propagated_exception = child.propagated_exception;
                    }
                    // An error upgrades any earlier decision.
                    self->decide(aggregate_op::Decision::Error);
                }
            } else if(cancelled) {
                // Attribution is recorded independently of the decision: even
                // when a winner already decided, a later external cancel can
                // settle the aggregate as Cancelled, and await_resume then
                // needs first_cancel_child to extract the cancel value.
                if(intercepts && self->first_cancel_child == aggregate_op::npos) {
                    self->first_cancel_child = self->find_child_index(child);
                }
                if(!self->decided()) {
                    self->decide(aggregate_op::Decision::Cancel);
                }
            } else if(self->kind == NodeKind::WhenAny && !self->decided()) {
                self->winner = self->find_child_index(child);
                self->decide(aggregate_op::Decision::Resume);
            }

            self->pending -= 1;
            return self->settle_if_idle();
        }

        case NodeKind::TaskGroup: {
            auto* self = static_cast<aggregate_op*>(this);
            assert(!self->settled && "task_group received child completion after settling");
            assert(self->pending > 0 && "task_group completed more children than it owns");

            if(child.state == Failed) {
                // A failed child aborts the group, and its error must survive
                // even a racing external cancel (errors outrank cancellation):
                // Decision::Error makes settle() resume the joiner, which
                // collects the error, instead of propagating the cancel past
                // it. The completing child still pins `pending`, so the
                // cascade cannot settle re-entrantly.
                const bool first_abort = !self->decided();
                self->decision = aggregate_op::Decision::Error;
                if(first_abort) {
                    self->cancel_children();
                }
            } else if(child.state == Cancelled && !self->decided()) {
                // A cancelled child aborts the group: cancel the remaining
                // children, but settle as a normal resume — join() reports
                // the collected errors.
                self->decide(aggregate_op::Decision::Resume);
            }

            // Eagerly reclaim the completed child's frame unless join()
            // still needs it for error extraction. The slot is tombstoned
            // (walkers and the cancel cascade skip null children) and
            // compacted by the next spawn(). Destroying the frame from its
            // own final suspension is the same pattern finalize() uses for
            // root tasks.
            if(child.state != Failed) {
                assert(child.kind == NodeKind::Task && "task_group children must be tasks");
                self->children[self->find_child_index(child)] = nullptr;
                self->reclaimed += 1;
                auto handle = static_cast<task_frame&>(child).handle();
#if KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
                pending_frame_destroys.push_back(handle);
#else
                handle.destroy();
#endif
            }

            self->pending -= 1;
            return self->settle_if_idle();
        }

        case NodeKind::MutexWaiter:
        case NodeKind::EventWaiter:
        case NodeKind::SystemIO:
        default: {
            std::abort();
        }
    }
}

}  // namespace kota
