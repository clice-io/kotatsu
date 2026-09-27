#include "kota/async/runtime/sync.h"

#include <cassert>
#include <utility>

#include "kota/async/io/loop.h"

namespace kota {

void sync_primitive::insert(wait_node& waiter) noexcept {
    assert(waiter.queue == nullptr && "waiter queued twice");
    waiter.queue = this;
    waiter.prev = tail;
    waiter.next = nullptr;
    (tail ? tail->next : head) = &waiter;
    tail = &waiter;
}

void sync_primitive::remove(wait_node& waiter) noexcept {
    assert(waiter.queue == this && "waiter not queued here");
    (waiter.prev ? waiter.prev->next : head) = waiter.next;
    (waiter.next ? waiter.next->prev : tail) = waiter.prev;
    waiter.prev = nullptr;
    waiter.next = nullptr;
    waiter.queue = nullptr;
}

bool sync_primitive::wake_one() {
    if(head == nullptr) {
        return false;
    }
    auto& waiter = *head;
    remove(waiter);
    waiter.grant(*this);
    return true;
}

wait_node::wait_node(condition_variable& owner, mutex& relock) noexcept :
    async_node(NodeKind::Waiter), owner(&owner), owner_kind(owner.kind), relock(&relock) {}

bool wait_node::await_ready() noexcept {
    switch(owner_kind) {
        case sync_primitive::Kind::Mutex: return static_cast<mutex*>(owner)->try_lock();
        case sync_primitive::Kind::Semaphore: return static_cast<semaphore*>(owner)->try_acquire();
        case sync_primitive::Kind::Event: return static_cast<event*>(owner)->is_set();
        case sync_primitive::Kind::ConditionVariable: return false;
    }
    std::unreachable();
}

std::coroutine_handle<> wait_node::wait(task_frame& waiting,
                                        std::source_location location) noexcept {
    if(auto ended = waiting.checkpoint()) {
        // A condition variable wait ends here still holding its mutex.
        return ended;
    }
    awaited_by(waiting, location);
    loop = &event_loop::current();
    if(relock != nullptr) {
        relock->unlock();
    }
    owner->insert(*this);
    return std::noop_coroutine();
}

void wait_node::grant(sync_primitive& from) {
    if(&from == owner) {
        if(owner_kind != sync_primitive::Kind::Event) {
            owner->woken += 1;
        }
        // A notification. Like a thread woken from std::condition_variable,
        // the wait now needs the mutex back.
        if(relock != nullptr && !relock->try_lock()) {
            relock->insert(*this);
            return;
        }
    }
    loop->defer_resume(*this);
}

void wait_node::cancel_wait() {
    if(queue != owner) {
        // Granted, or a notified condition variable wait queued on its mutex
        // again. Its task learns of the cancel when it is woken.
        give_back();
        return;
    }

    owner->remove(*this);
    if(relock != nullptr && !relock->try_lock()) {
        // A condition variable wait ends holding its mutex: the cancel waits
        // for it.
        relock->insert(*this);
        return;
    }
    state = State::Cancelled;
    resume_and_drain(std::exchange(parent, nullptr)->on_child_complete(*this));
}

void wait_node::give_back() {
    // An event grants nothing to hand on, and may be gone.
    if(owner_kind == sync_primitive::Kind::Event) {
        return;
    }
    owner->woken -= 1;
    switch(owner_kind) {
        case sync_primitive::Kind::Mutex: static_cast<mutex*>(owner)->unlock(); break;
        case sync_primitive::Kind::Semaphore: static_cast<semaphore*>(owner)->release(); break;
        case sync_primitive::Kind::ConditionVariable:
            // The notification; the wait keeps its mutex, which it holds or
            // waits for.
            static_cast<condition_variable*>(owner)->notify_one();
            break;
        case sync_primitive::Kind::Event: std::unreachable();
    }
}

void wait_node::resume() {
    auto& task = static_cast<task_frame&>(*parent);
    task.child = nullptr;
    if(cancel_requested) {
        // cancel_wait() has handed on what the wait was granted.
        resume_and_drain(task.finish(State::Cancelled));
        return;
    }
    if(owner_kind != sync_primitive::Kind::Event) {
        owner->woken -= 1;
    }
    resume_and_drain(task.handle());
}

}  // namespace kota
