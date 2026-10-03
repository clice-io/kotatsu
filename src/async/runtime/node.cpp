#include "kota/async/runtime/node.h"

#include <cassert>
#include <utility>
#include <vector>

#include "resumption.h"
#include "kota/async/io/loop.h"
#include "kota/async/runtime/sync.h"
#include "kota/async/runtime/task.h"

namespace kota {

namespace {

#if KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
/// MSVC's coroutine codegen under ASan cannot destroy a frame from its own
/// final suspension: frames wait here until the resumption that ended them
/// has returned.
thread_local std::vector<std::coroutine_handle<>> pending_frame_destroys;
#endif

void destroy_frame(std::coroutine_handle<> frame) {
#if KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    pending_frame_destroys.push_back(frame);
#else
    frame.destroy();
#endif
}

/// Destroys the frames that ended in the outermost resumption on this
/// thread, which has just returned: only now is none of them on the stack.
void destroy_ended_frames() {
#if KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    while(!pending_frame_destroys.empty()) {
        for(auto frame: std::exchange(pending_frame_destroys, {})) {
            frame.destroy();
        }
    }
#endif
}

/// A resumption is under way on this thread, or a ResumptionScope stands for
/// one.
thread_local bool draining = false;

}  // namespace

detail::ResumptionScope::ResumptionScope() noexcept : outermost(!std::exchange(draining, true)) {}

detail::ResumptionScope::~ResumptionScope() {
    if(outermost) {
        destroy_ended_frames();
        draining = false;
    }
}

void async_node::resume_and_drain(std::coroutine_handle<> handle) {
    const bool outermost = !std::exchange(draining, true);
    handle.resume();
    if(!outermost) {
        return;
    }
    if(event_loop::has_current()) {
        event_loop::current().drain_deferred();
    }
    destroy_ended_frames();
    draining = false;
}

void async_node::cancel() {
    if(done() || cancel_requested) {
        return;
    }
    cancel_requested = true;

    switch(kind) {
        case NodeKind::Task:
            // A task that has not started never will, and a running one ends
            // at its next suspending co_await; a suspended one passes it on.
            if(auto* awaited = static_cast<task_frame*>(this)->child) {
                awaited->cancel();
            }
            break;

        case NodeKind::Waiter: static_cast<wait_node*>(this)->cancel_wait(); break;

        case NodeKind::WhenAll:
        case NodeKind::WhenAny:
        case NodeKind::TaskGroup:
            resume_and_drain(static_cast<aggregate_op*>(this)->cancel_all());
            break;

        case NodeKind::SystemIO: {
            auto* self = static_cast<io_op*>(this);
            self->action(self);
            break;
        }
    }
}

void async_node::awaited_by(task_frame& waiting, std::source_location location) noexcept {
    this->location = location;
    parent = &waiting;
    waiting.child = this;
    state = State::Running;
}

std::coroutine_handle<> async_node::on_child_complete(async_node& child) {
    if(kind != NodeKind::Task) {
        return static_cast<aggregate_op*>(this)->child_completed(static_cast<task_frame&>(child));
    }

    auto& self = static_cast<task_frame&>(*this);
    self.child = nullptr;
    if(child.state == State::Cancelled && !child.intercept) {
        return self.finish(State::Cancelled);
    }
    if(child.state == State::Failed && child.kind == NodeKind::Task) {
        // A child that threw resumes its parent instead, whose co_await
        // rethrows.
        auto& failed = static_cast<task_frame&>(child);
        if(failed.hook && !failed.threw()) {
            failed.hook(failed, self);
            return self.finish(State::Failed);
        }
    }
    return self.handle();
}

std::coroutine_handle<> task_frame::start(async_node* parent) {
    assert(state == State::Pending && "a task starts once");
    this->parent = parent;
    if(cancel_requested) {
        return finish(State::Cancelled);
    }
    state = State::Running;
    return handle();
}

std::coroutine_handle<> task_frame::checkpoint() {
    return cancel_requested ? finish(State::Cancelled) : nullptr;
}

std::coroutine_handle<> task_frame::await_task(task_frame& awaited,
                                               bool intercept,
                                               error_hook hook,
                                               std::source_location location) {
    if(auto ended = checkpoint()) {
        // The awaited task never starts; its owner destroys it.
        return ended;
    }
    awaited.location = location;
    awaited.intercept = intercept;
    awaited.hook = hook;
    child = &awaited;
    return awaited.start(this);
}

std::coroutine_handle<> task_frame::finish(State end) {
    state = end;
    if(parent == nullptr) {
        if(owned_by_loop) {
            destroy_frame(handle());
        }
        return std::noop_coroutine();
    }
    return std::exchange(parent, nullptr)->on_child_complete(*this);
}

void detail::task_access::run_root(task_frame& root) {
    task_frame::resume_and_drain(root.start(nullptr));
}

void detail::task_access::drop_root(task_frame& root) {
    if(root.owned_by_loop) {
        // Never started, so not at its final suspension: nothing to defer.
        root.handle().destroy();
    } else {
        root.scheduled = false;
    }
}

void aggregate_op::abandon_children() {
    // A child the cancel ends at once, or resumes when it catches that, may
    // run on here; it must not add to the group.
    cancel_requested = true;
    while(auto* child = head) {
        unlink(*child);
        child->parent = nullptr;
        child->hook = nullptr;
        child->owned_by_loop = true;
        // May end the child, and free it, at once.
        child->cancel();
    }
}

void aggregate_op::link(task_frame& child) noexcept {
    child.prev_sibling = tail;
    child.next_sibling = nullptr;
    (tail ? tail->next_sibling : head) = &child;
    tail = &child;
}

void aggregate_op::unlink(task_frame& child) noexcept {
    (child.prev_sibling ? child.prev_sibling->next_sibling : head) = child.next_sibling;
    (child.next_sibling ? child.next_sibling->prev_sibling : tail) = child.prev_sibling;
    child.prev_sibling = nullptr;
    child.next_sibling = nullptr;
}

task_frame& aggregate_op::watch(task_frame& child, error_hook hook) noexcept {
    child.hook = hook;
    return child;
}

std::coroutine_handle<> aggregate_op::arm(task_frame& waiting,
                                          std::span<task_frame* const> children,
                                          std::source_location location) {
    if(auto ended = waiting.checkpoint()) {
        // None of the children starts; they go with the aggregate.
        return ended;
    }
    awaited_by(waiting, location);
    pending = children.size();
    for(auto* child: children) {
        child->location = location;
        link(*child);
    }

    {
        // A child that ends at once and decides the outcome cancels the ones
        // after it, which then end without running.
        pin held(*this);
        for(auto* child: children) {
            resume_and_drain(child->start(this));
        }
    }
    return settle_if_idle();
}

void aggregate_op::spawn(task_frame& child, std::source_location location) {
    child.location = location;
    link(child);
    pending += 1;
    resume_and_drain(child.start(this));
}

std::coroutine_handle<> aggregate_op::await_children(task_frame& waiting,
                                                     std::source_location location) {
    awaited_by(waiting, location);
    if(waiting.cancel_requested) {
        // The checkpoint. The children already run, so they cannot just be
        // dropped: cancel them, and settling once they have ended ends the
        // task.
        cancel_requested = true;
        return cancel_all();
    }
    return std::noop_coroutine();
}

std::coroutine_handle<> aggregate_op::child_completed(task_frame& child) {
    assert(pending > 0 && "an aggregate saw more children end than it started");
    unlink(child);

    // The child is counted off only below, so the cancels decide() cascades
    // cannot settle the aggregate under this call.
    switch(child.state) {
        case State::Failed:
            if(child.threw()) {
#if KOTA_ENABLE_EXCEPTIONS
                if(!exception) {
                    exception = child.exception;
                }
#endif
            } else {
                child.hook(child, *this);
            }
            decide(Decision::Error);
            break;

        case State::Cancelled:
            // A task_group child that ends cancelled just ends: its siblings
            // run on.
            if(kind != NodeKind::TaskGroup) {
                decide(Decision::Cancel);
            }
            break;

        case State::Succeeded:
            if(kind == NodeKind::WhenAny && !decided()) {
                winner = &child;
                decide(Decision::Resume);
            }
            break;

        case State::Pending:
        case State::Running: std::unreachable();
    }

    if(kind == NodeKind::TaskGroup) {
        // A group keeps no child that has ended.
        destroy_frame(child.handle());
    }
    pending -= 1;
    return settle_if_idle();
}

void aggregate_op::decide(Decision d) {
    if(decided()) {
        if(d == Decision::Error) {
            decision = Decision::Error;
        }
        return;
    }
    decision = d;
    cancel_children();
}

void aggregate_op::cancel_children() {
    // Cancelling one child can end any of them, which unlinks it and may even
    // destroy it, so every step takes the head afresh after moving it to the
    // tail. The children not reached yet stay ahead of the ones that were, and
    // none is added meanwhile, so as many steps as there are children reach
    // them all; a child cancelled twice ignores the second cancel.
    std::size_t count = 0;
    for(auto* child = head; child != nullptr; child = child->next_sibling) {
        count += 1;
    }
    for(; count > 0 && head != nullptr; --count) {
        auto& child = *head;
        unlink(child);
        link(child);
        child.cancel();
    }
}

std::coroutine_handle<> aggregate_op::cancel_all() {
    {
        pin held(*this);
        cancel_children();
    }
    return settle_if_idle();
}

async_node::State aggregate_op::settled_state() const noexcept {
    if(decision == Decision::Error) {
        return State::Failed;
    }
    if(decision == Decision::Cancel || cancel_requested) {
        return State::Cancelled;
    }
    return State::Succeeded;
}

std::coroutine_handle<> aggregate_op::settle_if_idle() {
    if(pending != 0 || parent == nullptr) {
        return std::noop_coroutine();
    }
    state = settled_state();
    return std::exchange(parent, nullptr)->on_child_complete(*this);
}

std::coroutine_handle<> io_op::attach(task_frame& waiting, std::source_location location) noexcept {
    awaited_by(waiting, location);
    if(waiting.cancel_requested) {
        // The checkpoint. The operation may already be in flight, so it cannot
        // just be dropped: cancel it, and its completion ends the task.
        cancel();
    }
    return std::noop_coroutine();
}

void io_op::complete_deferred(event_loop& loop) noexcept {
    loop.defer_complete(*this);
}

void io_op::complete() noexcept {
    state = cancel_requested() ? State::Cancelled : State::Succeeded;
    auto* awaiting = std::exchange(parent, nullptr);
    assert(awaiting != nullptr && "io_op completed while no task awaits it");
    resume_and_drain(awaiting->on_child_complete(*this));
}

}  // namespace kota
