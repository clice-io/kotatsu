#include "eventide/async/sync.h"

#include <cassert>

namespace eventide {

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

bool sync_primitive::cancel_waiter(waiter_link* link) noexcept {
    if(!link) {
        return false;
    }

    auto* awaiting = link->awaiter;
    link->awaiter = nullptr;
    if(!awaiting || awaiting->is_cancelled()) {
        return false;
    }

    link->state = async_node::Cancelled;
    link->policy = static_cast<async_node::Policy>(link->policy | async_node::InterceptCancel);
    auto next = awaiting->handle_subtask_result(link);
    if(next) {
        next.resume();
    }
    return true;
}

}  // namespace eventide
