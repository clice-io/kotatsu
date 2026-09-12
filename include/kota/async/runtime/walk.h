#pragma once

#include <set>

#include "kota/async/runtime/node.h"
#include "kota/async/runtime/sync.h"

namespace kota {

const inline async_node* get_parent(const async_node& node) {
    using NK = async_node::NodeKind;
    switch(node.kind) {
        case NK::Task: return static_cast<const task_frame&>(node).get_parent();
        case NK::MutexWaiter:
        case NK::EventWaiter: return static_cast<const wait_node&>(node).get_parent();
        case NK::WhenAll:
        case NK::WhenAny:
        case NK::TaskGroup: return static_cast<const aggregate_op&>(node).get_parent();
        case NK::SystemIO: return static_cast<const io_op&>(node).get_parent();
    }
    return nullptr;
}

const inline sync_primitive* get_resource(const async_node& node) {
    using NK = async_node::NodeKind;
    switch(node.kind) {
        case NK::MutexWaiter:
        case NK::EventWaiter: return static_cast<const wait_node&>(node).get_resource();
        default: return nullptr;
    }
}

template <typename Derived>
class async_visitor {
public:
    void walk_node(const async_node& node) {
        if(!visited_.insert(&node).second) {
            return;
        }

        using NK = async_node::NodeKind;
        switch(node.kind) {
            case NK::Task: {
                auto& task = static_cast<const task_frame&>(node);
                if(!derived().visit_task(task))
                    return;
                // An executing task stores itself as a sentinel child;
                // that is not an edge.
                if(auto* child = task.get_child(); child && child != &task) {
                    derived().visit_edge(&task, child);
                    walk_node(*child);
                }
                break;
            }

            case NK::MutexWaiter:
            case NK::EventWaiter: {
                auto& wn = static_cast<const wait_node&>(node);
                if(!derived().visit_wait_node(wn))
                    return;
                if(auto* res = wn.get_resource()) {
                    derived().visit_edge(&wn, res);
                    walk_sync(*res);
                }
                break;
            }

            case NK::WhenAll:
            case NK::WhenAny:
            case NK::TaskGroup: {
                auto& agg = static_cast<const aggregate_op&>(node);
                if(!derived().visit_aggregate(agg))
                    return;
                for(auto* child: agg.get_children()) {
                    if(child) {
                        derived().visit_edge(&agg, child);
                        walk_node(*child);
                    }
                }
                break;
            }

            case NK::SystemIO: {
                derived().visit_io(static_cast<const io_op&>(node));
                break;
            }
        }
    }

    void walk_sync(const sync_primitive& resource) {
        if(!visited_.insert(&resource).second) {
            return;
        }

        if(!derived().visit_sync(resource))
            return;
        for(auto* w = resource.get_head(); w; w = w->get_next()) {
            derived().visit_edge(&resource, w);
            walk_node(*w);
        }
    }

    void reset() {
        visited_.clear();
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

    bool visit_io(const io_op&) {
        return true;
    }

    bool visit_sync(const sync_primitive&) {
        return true;
    }

    void visit_edge(const void*, const void*) {}

private:
    Derived& derived() {
        return static_cast<Derived&>(*this);
    }

    std::set<const void*> visited_;
};

}  // namespace kota
