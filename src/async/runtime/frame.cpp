#include "kota/async/runtime/frame.h"

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

void enqueue_destroy(std::coroutine_handle<> handle) {
    if(handle) {
#if KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
        pending_frame_destroys.push_back(handle);
#else
        handle.destroy();
#endif
    }
}

#if KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
void drain_pending_destroys() {
    while(!pending_frame_destroys.empty()) {
        auto queued = std::move(pending_frame_destroys);
        pending_frame_destroys.clear();
        for(auto handle: queued) {
            if(handle) {
                handle.destroy();
            }
        }
    }
}
#endif

}  // namespace

void detail::resume_and_drain(std::coroutine_handle<> handle) {
    if(handle) {
        handle.resume();
    }
#if KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    drain_pending_destroys();
#endif
}

void async_node::intercept_cancel() noexcept {
    policy = static_cast<Policy>(policy | InterceptCancel);
}

std::coroutine_handle<> aggregate_op::deliver_deferred() noexcept {
    if(deferred == Deferred::None || !awaiter) {
        return std::noop_coroutine();
    }

    phase = Phase::Settled;

    assert(awaiter->is_standard_task() && "aggregate awaiter must be a task");
    awaiter->clear_awaitee();

    switch(deferred) {
        case Deferred::Resume: return static_cast<standard_task*>(awaiter)->handle();

        case Deferred::Cancel:
            if(policy & InterceptCancel) {
                state = Cancelled;
                return static_cast<standard_task*>(awaiter)->handle();
            }
            awaiter->state = Cancelled;
            return awaiter->final_transition();

        case Deferred::Error: return static_cast<standard_task*>(awaiter)->handle();

        case Deferred::None: break;
    }

    std::abort();
}

void async_node::clear_awaitee() noexcept {
    if(kind == NodeKind::Task) {
        static_cast<standard_task*>(this)->set_awaitee(nullptr);
    }
}

/// Recursively cancels this node and all of its descendants.
/// Idempotent: re-cancelling an already-cancelled or failed node is a no-op.
void async_node::cancel() {
    if(state == Cancelled || state == Failed) {
        return;
    }
    state = Cancelled;

    auto propagate_cancel = [](waiter_link* link) {
        if(!link) {
            return;
        }

        auto* awaiter = link->awaiter;
        link->awaiter = nullptr;
        if(!awaiter) {
            return;
        }

        auto next = awaiter->handle_subtask_result(link);
        detail::resume_and_drain(next);
    };

    switch(kind) {
        case NodeKind::Task: {
            auto* self = static_cast<standard_task*>(this);
            if(self->awaitee) {
                self->awaitee->cancel();
            }
            break;
        }
        case NodeKind::MutexWaiter:
        case NodeKind::EventWaiter: {
            auto* self = static_cast<waiter_link*>(this);
            if(auto* res = self->resource) {
                res->remove(self);
            }
            propagate_cancel(self);
            break;
        }

        case NodeKind::WhenAll:
        case NodeKind::WhenAny:
        case NodeKind::Scope: {
            auto* self = static_cast<aggregate_op*>(this);
            const bool was_arming = self->phase == aggregate_op::Phase::Arming;
            self->phase = aggregate_op::Phase::Cancelling;
            for(auto* child: self->awaitees) {
                if(child) {
                    child->cancel();
                }
            }
            self->defer_cancel();
            self->phase = was_arming ? aggregate_op::Phase::Arming : aggregate_op::Phase::Settled;

            if(was_arming) {
                break;
            }

            auto next = self->deliver_deferred();
            detail::resume_and_drain(next);
            break;
        }

        case NodeKind::SystemIO: {
            auto* self = static_cast<system_op*>(this);
            if(self->action) {
                self->action(self);
            }
            break;
        }
    }
}

/// Resumes a standard task's coroutine, unless it has been cancelled.
void async_node::resume() {
    if(is_standard_task()) {
        if(!is_cancelled() && !is_failed()) {
            static_cast<standard_task*>(this)->handle().resume();
#if KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
            drain_pending_destroys();
#endif
        }
    }
}

/// Called by libuv callbacks when an I/O operation completes.
/// Preserves Cancelled state if already set, then notifies the parent.
void system_op::complete() noexcept {
    if(state != Cancelled) {
        state = Finished;
    }
    auto* parent = awaiter;
    awaiter = nullptr;
    if(!parent) {
        return;
    }
    auto next = parent->handle_subtask_result(this);
    detail::resume_and_drain(next);
}

/// Wires this node as a child of `awaiter`. For Task nodes, sets state
/// to Running and returns the coroutine handle (ready to resume).
/// For transient nodes (waiter_link, system_op), records the awaiter
/// and returns noop_coroutine (resumed later by event/complete).
std::coroutine_handle<> async_node::link_continuation(async_node* awaiter,
                                                      std::source_location loc) {
    this->location = loc;
    if(awaiter->kind == NodeKind::Task) {
        auto p = static_cast<standard_task*>(awaiter);
        p->awaitee = this;
    }

    switch(this->kind) {
        case NodeKind::Task: {
            auto self = static_cast<standard_task*>(this);
            self->state = Running;
            self->awaiter = awaiter;
            return self->handle();
        }

        case NodeKind::MutexWaiter:
        case NodeKind::EventWaiter: {
            auto self = static_cast<waiter_link*>(this);
            self->awaiter = awaiter;
            return std::noop_coroutine();
        }
        case NodeKind::WhenAll:
        case NodeKind::WhenAny:
        case NodeKind::Scope: break;
        case NodeKind::SystemIO: {
            auto self = static_cast<system_op*>(this);
            self->awaiter = awaiter;
            return std::noop_coroutine();
        }
    }

    std::abort();
}

/// Called when a task reaches final_suspend (Finished, Cancelled, or Failed).
/// For root tasks with no awaiter, destroys the coroutine frame.
/// Otherwise, notifies the parent via handle_subtask_result.
std::coroutine_handle<> async_node::final_transition() {
    switch(kind) {
        case NodeKind::Task: {
            auto p = static_cast<standard_task*>(this);
            if(!p->awaiter) {
                if(p->root) {
                    enqueue_destroy(p->handle());
                }
                return std::noop_coroutine();
            }

            return p->awaiter->handle_subtask_result(p);
        }

        case NodeKind::MutexWaiter:
        case NodeKind::EventWaiter:
        case NodeKind::WhenAll:
        case NodeKind::WhenAny:
        case NodeKind::Scope:
        case NodeKind::SystemIO: break;
    }

    std::abort();
}

/// Dispatches a child's completion to its parent node.
///
/// For Task parents: resumes the coroutine normally for Finished/Failed,
///   or propagates cancellation upward.
/// For Aggregate parents (when_all/when_any/scope):
///   - Cancellation: cancels all siblings, propagates upward.
///   - Failed child (exception or structured error): cancels all siblings, resumes awaiter.
///   - WhenAny completion: records winner, cancels siblings, resumes awaiter.
///   - WhenAll/Scope completion: increments counter, resumes awaiter when all done.
std::coroutine_handle<> async_node::handle_subtask_result(async_node* child) {
    assert(child && child != this && "invalid parameter!");

    switch(kind) {
        case NodeKind::Task: {
            auto self = static_cast<standard_task*>(this);

            if(child->state == Cancelled) {
                if(child->policy & InterceptCancel) {
                    self->awaitee = nullptr;
                    return self->handle();
                }

                self->awaitee = nullptr;
                self->state = Cancelled;
                return self->final_transition();
            }

            // Finished or Failed: resume parent normally.
            // await_resume handles exceptions (rethrow_if_exception)
            // and errors (explicit return value inspection).
            self->awaitee = nullptr;
            // If the child task has an error hook (set by or_fail_task_await),
            // let the hook handle error propagation instead of resuming normally.
            if(child->state == Failed && child->kind == NodeKind::Task) {
                auto* child_task = static_cast<standard_task*>(child);
                if(auto propagate = child_task->get_error_hook()) {
                    return propagate(child, self);
                }
            }
            return self->handle();
        }

        case NodeKind::WhenAll:
        case NodeKind::WhenAny:
        case NodeKind::Scope: {
            auto self = static_cast<aggregate_op*>(this);
            if(self->is_settled()) {
                return std::noop_coroutine();
            }

            const bool aggregate_catches_cancel = self->policy & InterceptCancel;
            const bool cancelled = child->state == Cancelled &&
                                   (aggregate_catches_cancel || !(child->policy & InterceptCancel));
            const bool failed = child->state == Failed;

            auto record_child_index = [&](std::size_t& slot) {
                if(slot != aggregate_op::npos) {
                    return;
                }
                for(std::size_t i = 0; i < self->awaitees.size(); ++i) {
                    if(self->awaitees[i] == child) {
                        slot = i;
                        return;
                    }
                }
            };

            if(cancelled || failed) {
                if(cancelled) {
                    if(aggregate_catches_cancel) {
                        record_child_index(self->first_cancel_child);
                    }
                    self->defer_cancel();
                }
                if(failed) {
                    const bool first_error = self->deferred != aggregate_op::Deferred::Error;
                    self->defer_error();
                    if(first_error) {
                        record_child_index(self->first_error_child);
                    }
                    if(first_error && child->propagated_exception) {
                        self->propagated_exception = child->propagated_exception;
                    }
                }

                if(self->is_deferring()) {
                    return std::noop_coroutine();
                }

                self->phase = aggregate_op::Phase::Settled;

                for(auto* other: self->awaitees) {
                    if(other && other != child) {
                        other->cancel();
                    }
                }

                return self->deliver_deferred();
            }

            if(self->kind == NodeKind::WhenAny) {
                if(self->winner == aggregate_op::npos) {
                    for(std::size_t i = 0; i < self->awaitees.size(); ++i) {
                        if(self->awaitees[i] == child) {
                            self->winner = i;
                            break;
                        }
                    }
                }

                const bool deferring = self->is_deferring();
                self->phase = aggregate_op::Phase::Settled;
                for(auto* other: self->awaitees) {
                    if(other && other != child) {
                        other->cancel();
                    }
                }

                self->defer_resume();
                if(deferring) {
                    return std::noop_coroutine();
                }

                return self->deliver_deferred();
            }

            self->completed += 1;
            if(self->completed >= self->total) {
                const bool deferring = self->is_deferring();
                self->phase = aggregate_op::Phase::Settled;
                self->defer_resume();
                if(deferring) {
                    return std::noop_coroutine();
                }

                return self->deliver_deferred();
            }

            return std::noop_coroutine();
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
