#pragma once

#include <set>
#include <source_location>

#include "kota/async/runtime/node.h"
#include "kota/async/runtime/sync.h"
#include "kota/async/runtime/task.h"

namespace kota {

/// Walks the task tree below a node, down to the sync primitives its waiters
/// wait on and the other waiters queued there. `Derived` gets a visit_* call
/// for every node and primitive, once per walk, and a visit_edge() call for
/// every link it follows; a visit_* returning false keeps the walk out of what
/// lies below that node. Pending I/O has nothing below it.
template <typename Derived>
class async_visitor {
public:
    void walk_node(const async_node& node) {
        if(!visited.insert(&node).second) {
            return;
        }

        using NK = async_node::NodeKind;
        switch(node.kind) {
            case NK::Task: {
                auto& task = static_cast<const task_frame&>(node);
                if(!derived().visit_task(task)) {
                    return;
                }
                if(auto* child = task.child) {
                    derived().visit_edge(&task, child);
                    walk_node(*child);
                }
                break;
            }

            case NK::Waiter: {
                auto& waiter = static_cast<const wait_node&>(node);
                if(!derived().visit_wait_node(waiter)) {
                    return;
                }
                if(auto* queue = waiter.queue) {
                    derived().visit_edge(&waiter, queue);
                    walk_sync(*queue);
                }
                break;
            }

            case NK::WhenAll:
            case NK::WhenAny:
            case NK::TaskGroup: {
                auto& aggregate = static_cast<const aggregate_op&>(node);
                if(!derived().visit_aggregate(aggregate)) {
                    return;
                }
                for(auto* child = aggregate.head; child != nullptr; child = child->next_sibling) {
                    derived().visit_edge(&aggregate, child);
                    walk_node(*child);
                }
                break;
            }

            case NK::SystemIO: derived().visit_io(static_cast<const io_op&>(node)); break;
        }
    }

    template <typename T, typename E, typename C>
    void walk(task<T, E, C>& root) {
        walk_node(detail::task_access::promise(root));
    }

    void walk_sync(const sync_primitive& resource) {
        if(!visited.insert(&resource).second) {
            return;
        }

        if(!derived().visit_sync(resource)) {
            return;
        }
        for(auto* waiter = resource.head; waiter != nullptr; waiter = waiter->next) {
            derived().visit_edge(&resource, waiter);
            walk_node(*waiter);
        }
    }

    void reset() {
        visited.clear();
    }

protected:
    bool visit_task(const task_frame&) {
        return true;
    }

    bool visit_wait_node(const wait_node&) {
        return true;
    }

    bool visit_aggregate(const aggregate_op&) {
        return true;
    }

    void visit_io(const io_op&) {}

    bool visit_sync(const sync_primitive&) {
        return true;
    }

    void visit_edge(const void*, const void*) {}

    static async_node::State state_of(const async_node& node) noexcept {
        return node.state;
    }

    const static std::source_location& location_of(const async_node& node) noexcept {
        return node.location;
    }

    /// The node awaiting `node`; null for a root and a node that has ended.
    const static async_node* parent_of(const async_node& node) noexcept {
        return node.parent;
    }

private:
    Derived& derived() {
        return static_cast<Derived&>(*this);
    }

    std::set<const void*> visited;
};

}  // namespace kota
