#include "eventide/async/sync.h"

#include <cassert>

namespace eventide {

void sync_primitive::insert(waiter_link* link) {
    assert(link && "insert: null waiter_link");
    assert(link->resource == nullptr && "insert: waiter_link already linked");
    assert(link->prev == nullptr && link->next == nullptr && "insert: waiter_link has links");

    link->resource = this;
    // Snapshot semantics for interrupt() depend on each waiter remembering the
    // generation that was current when it joined the queue.
    link->generation = waiter_generation;

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

bool sync_primitive::cancel_waiter(waiter_link* link) noexcept {
    if(!link) {
        return false;
    }

    auto* awaiting = link->awaiter;
    link->awaiter = nullptr;
    if(!awaiting || awaiting->is_cancelled()) {
        return false;
    }

    // This callback may resume arbitrary user code immediately. In particular,
    // that code may enqueue a brand-new waiter on the same resource, or even
    // destroy other waiters that used to be siblings of `link`. That is why
    // interrupt() must process the queue in-place with generation checks rather
    // than first stashing raw waiter pointers into a temporary container.
    link->state = async_node::Cancelled;
    link->policy = static_cast<async_node::Policy>(link->policy | async_node::InterceptCancel);
    auto next = awaiting->handle_subtask_result(link);
    if(next) {
        next.resume();
    }
    return true;
}

}  // namespace eventide
