#include "eventide/async/frame.h"

#include <cassert>
#include <utility>

#include "libuv.h"
#include "eventide/async/loop.h"

namespace eventide {

/// Tracks the most recently resumed task (used for debugging/profiling).
static thread_local async_node* current_node = nullptr;

void async_node::clear_awaitee() noexcept {
    if(kind == NodeKind::Task) {
        static_cast<standard_task*>(this)->set_awaitee(nullptr);
    }
}

/// Recursively cancels this node and all of its descendants.
/// Idempotent: re-cancelling an already-cancelled node is a no-op.
void async_node::cancel() {
    if(state == Cancelled) {
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
        if(next) {
            next.resume();
        }
    };

    switch(kind) {
        case NodeKind::Task: {
            auto* self = static_cast<standard_task*>(this);
            if(self->awaitee) {
                self->awaitee->cancel();
            }
            break;
        }
        case NodeKind::Mutex:
        case NodeKind::Event:
        case NodeKind::Semaphore:
        case NodeKind::ConditionVariable: {
            auto* self = static_cast<sync_primitive*>(this);

            while(auto* cur = self->pop_waiter()) {
                cur->state = Cancelled;
                propagate_cancel(cur);
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
            self->done = true;
            for(auto* child: self->awaitees) {
                if(child) {
                    child->cancel();
                }
            }
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
        if(!is_cancelled()) {
            static_cast<standard_task*>(this)->handle().resume();
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
    if(next) {
        next.resume();
    }
}

/// Wires this node as a child of `awaiter`. For Task nodes, sets state
/// to Running and returns the coroutine handle (ready to resume).
/// For transient nodes (waiter_link, system_op), records the awaiter
/// and returns noop_coroutine (resumed later by event/complete).
std::coroutine_handle<> async_node::link_continuation(async_node* awaiter,
                                                      std::source_location location) {
    this->location = location;
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

        case NodeKind::Mutex:
        case NodeKind::Event:
        case NodeKind::Semaphore:
        case NodeKind::ConditionVariable: {
            /// Shared resources are never linked as children; they are
            /// awaited via their waiter_link sub-objects.
            std::abort();
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

/// Called when a task reaches final_suspend (Finished or Cancelled).
/// For root tasks with no awaiter, destroys the coroutine frame.
/// Otherwise, notifies the parent via handle_subtask_result.
std::coroutine_handle<> async_node::final_transition() {
    switch(kind) {
        case NodeKind::Task: {
            auto p = static_cast<standard_task*>(this);
            if(!p->awaiter) {
                if(p->root) {
                    p->handle().destroy();
                }
                return std::noop_coroutine();
            }

            return p->awaiter->handle_subtask_result(p);
        }

        case NodeKind::Mutex:
        case NodeKind::Event:
        case NodeKind::Semaphore:
        case NodeKind::ConditionVariable:
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
/// For Task parents: resumes the coroutine, or propagates cancellation upward.
/// For Aggregate parents (when_all/when_any/scope):
///   - Cancellation: cancels all siblings, propagates upward.
///   - WhenAny completion: records winner, cancels siblings, resumes awaiter.
///   - WhenAll/Scope completion: increments counter, resumes awaiter when all done.
std::coroutine_handle<> async_node::handle_subtask_result(async_node* child) {
    assert(child && child != this && "invalid parameter!");

    switch(kind) {
        case NodeKind::Task: {
            auto self = static_cast<standard_task*>(this);

            if(child->state == Finished) {
                self->awaitee = nullptr;
                current_node = self;
                return self->handle();
            }

            if(child->state == Cancelled) {
                /// InterceptCancel: the parent handles cancellation explicitly
                /// (e.g., catch_cancel() or with_token()). Resume as normal.
                if(child->policy & InterceptCancel) {
                    self->awaitee = nullptr;
                    current_node = self;
                    return self->handle();
                }

                self->awaitee = nullptr;
                self->state = Cancelled;
                return self->final_transition();
            }

            std::abort();
        }

        case NodeKind::WhenAll:
        case NodeKind::WhenAny:
        case NodeKind::Scope: {
            auto self = static_cast<aggregate_op*>(this);
            if(self->done) {
                return std::noop_coroutine();
            }

            const bool cancelled = child->state == Cancelled && !(child->policy & InterceptCancel);
            if(cancelled) {
                self->done = true;
                self->pending_cancel = true;

                for(auto* other: self->awaitees) {
                    if(other && other != child) {
                        other->cancel();
                    }
                }

                if(self->arming) {
                    self->pending_resume = true;
                    return std::noop_coroutine();
                }

                if(self->awaiter) {
                    self->awaiter->clear_awaitee();
                    self->awaiter->state = Cancelled;
                    return self->awaiter->final_transition();
                }

                return std::noop_coroutine();
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

                self->done = true;
                for(auto* other: self->awaitees) {
                    if(other && other != child) {
                        other->cancel();
                    }
                }

                if(self->arming) {
                    self->pending_resume = true;
                    return std::noop_coroutine();
                }

                if(self->awaiter) {
                    assert(self->awaiter->is_standard_task() && "aggregate awaiter must be a task");
                    self->awaiter->clear_awaitee();
                    return static_cast<standard_task*>(self->awaiter)->handle();
                }

                return std::noop_coroutine();
            }

            self->completed += 1;
            if(self->completed >= self->total) {
                self->done = true;
                if(self->arming) {
                    self->pending_resume = true;
                    return std::noop_coroutine();
                }

                if(self->awaiter) {
                    assert(self->awaiter->is_standard_task() && "aggregate awaiter must be a task");
                    self->awaiter->clear_awaitee();
                    return static_cast<standard_task*>(self->awaiter)->handle();
                }
            }

            return std::noop_coroutine();
        }

        case NodeKind::Mutex:
        case NodeKind::Event:
        case NodeKind::Semaphore:
        case NodeKind::ConditionVariable:
        case NodeKind::MutexWaiter:
        case NodeKind::EventWaiter:
        case NodeKind::SystemIO:
        default: {
            std::abort();
        }
    }
}

void sync_primitive::insert(waiter_link* link) {
    assert(link && "insert: null waiter_link");
    assert(link->resource == nullptr && "insert: waiter_link already linked");
    assert(link->prev == nullptr && link->next == nullptr && "insert: waiter_link has links");

    link->resource = this;

    if(tail) {
        tail->next = link;
        link->prev = tail;
        tail = link;
    } else {
        head = link;
        tail = link;
    }
}

void sync_primitive::remove(waiter_link* link) {
    assert(link && "remove: null waiter_link");
    assert(link->resource == this && "remove: waiter_link not owned by resource");

    if(link->prev) {
        link->prev->next = link->next;
    } else {
        head = link->next;
    }

    if(link->next) {
        link->next->prev = link->prev;
    } else {
        tail = link->prev;
    }

    link->prev = nullptr;
    link->next = nullptr;
    link->resource = nullptr;
}

}  // namespace eventide
