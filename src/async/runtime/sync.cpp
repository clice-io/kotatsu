#include "kota/async/runtime/sync.h"

#include <cassert>

#include "kota/async/io/loop.h"

namespace kota {

void sync_primitive::insert(wait_node* link) {
    assert(link && "insert: null wait_node");
    assert(link->resource == nullptr && "insert: wait_node already linked");
    assert(link->prev == nullptr && link->next == nullptr && "insert: wait_node has links");

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

void sync_primitive::remove(wait_node* link) {
    assert(link && "remove: null wait_node");
    assert(link->resource == this && "remove: wait_node not owned by resource");

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

bool sync_primitive::resume_waiter(wait_node& link) noexcept {
    auto* awaiting = link.parent;
    assert(awaiting && "resume_waiter: waiter has no parent");
    assert(event_loop::has_current() && "resume_waiter: no event loop on this thread");
    if(awaiting->is_cancelled()) {
        link.parent = nullptr;
        return false;
    }
    link.state = async_node::Finished;
    event_loop::current().defer_resume(*awaiting);
    return true;
}

bool sync_primitive::cancel_waiter(wait_node& link) noexcept {
    auto* awaiting = link.parent;
    assert(awaiting && "cancel_waiter: waiter has no parent");
    assert(event_loop::has_current() && "cancel_waiter: no event loop on this thread");
    if(awaiting->is_cancelled()) {
        link.parent = nullptr;
        return false;
    }
    // The deferred resume re-enters the awaiting coroutine directly, so the
    // awaiter's await_resume observes state == Cancelled and reports the
    // interruption as a value; nothing consults the waiter's policy.
    link.state = async_node::Cancelled;
    event_loop::current().defer_resume(*awaiting);
    return true;
}

}  // namespace kota
