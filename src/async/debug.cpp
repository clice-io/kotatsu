#include <format>
#include <set>
#include <string>
#include <string_view>

#include "eventide/async/frame.h"

namespace eventide {

static std::string_view kind_name(async_node::NodeKind k) {
    switch(k) {
        case async_node::NodeKind::Task: return "Task";
        case async_node::NodeKind::Mutex: return "Mutex";
        case async_node::NodeKind::Event: return "Event";
        case async_node::NodeKind::Semaphore: return "Semaphore";
        case async_node::NodeKind::ConditionVariable: return "ConditionVariable";
        case async_node::NodeKind::MutexWaiter: return "MutexWaiter";
        case async_node::NodeKind::EventWaiter: return "EventWaiter";
        case async_node::NodeKind::WhenAll: return "WhenAll";
        case async_node::NodeKind::WhenAny: return "WhenAny";
        case async_node::NodeKind::Scope: return "Scope";
        case async_node::NodeKind::SystemIO: return "SystemIO";
    }
    return "Unknown";
}

static std::string_view state_name(async_node::State s) {
    switch(s) {
        case async_node::Pending: return "Pending";
        case async_node::Running: return "Running";
        case async_node::Cancelled: return "Cancelled";
        case async_node::Finished: return "Finished";
    }
    return "Unknown";
}

static std::string node_id(const async_node* node) {
    return std::format("n{:x}", reinterpret_cast<std::uintptr_t>(node));
}

static std::string_view basename(const char* path) {
    if(!path || path[0] == '\0') {
        return {};
    }
    std::string_view sv(path);
    auto pos = sv.find_last_of("/\\");
    return pos != std::string_view::npos ? sv.substr(pos + 1) : sv;
}

static void emit_node(const async_node* node, std::string& out) {
    auto file = basename(node->location.file_name());
    std::string label;
    if(!file.empty()) {
        label = std::format("{}\\n{}\\n{}:{}",
                            kind_name(node->kind),
                            state_name(node->state),
                            file,
                            node->location.line());
    } else {
        label = std::format("{}\\n{}", kind_name(node->kind), state_name(node->state));
    }

    std::string_view shape = "box";
    std::string_view color = "white";

    if(node->is_standard_task()) {
        switch(node->state) {
            case async_node::Running: color = "\"#90EE90\""; break;
            case async_node::Finished: color = "\"#D3D3D3\""; break;
            case async_node::Cancelled: color = "\"#FFB6C1\""; break;
            default: break;
        }
    } else if(node->is_sync_primitive()) {
        shape = "ellipse";
        color = "\"#ADD8E6\"";
    } else if(node->is_aggregate_op()) {
        shape = "diamond";
        color = "\"#D8BFD8\"";
    } else if(node->kind == async_node::NodeKind::SystemIO) {
        color = "\"#FFFFE0\"";
    } else if(node->is_waiter_link()) {
        color = "\"#FFDAB9\"";
    }

    std::format_to(std::back_inserter(out),
                   "  {} [label=\"{}\", shape={}, style=filled, fillcolor={}];\n",
                   node_id(node),
                   label,
                   shape,
                   color);
}

static void emit_edge(const async_node* from, const async_node* to, std::string& out) {
    std::format_to(std::back_inserter(out), "  {} -> {};\n", node_id(from), node_id(to));
}

/// Returns the awaiter (parent) of a node, or nullptr for roots / sync_primitives.
const async_node* async_node::get_awaiter(const async_node* node) {
    switch(node->kind) {
        case NodeKind::Task: return static_cast<const standard_task*>(node)->awaiter;
        case NodeKind::MutexWaiter:
        case NodeKind::EventWaiter: {
            // In the graph, a waiter_link's structural parent is the sync_primitive
            // it belongs to (via resource), not the task awaiting on it.
            auto* link = static_cast<const waiter_link*>(node);
            if(link->resource) {
                return link->resource;
            }
            return link->awaiter;
        }
        case NodeKind::WhenAll:
        case NodeKind::WhenAny:
        case NodeKind::Scope: return static_cast<const aggregate_op*>(node)->awaiter;
        case NodeKind::SystemIO: return static_cast<const system_op*>(node)->awaiter;
        default: return nullptr;
    }
}

void async_node::dump_dot_walk(const async_node* node,
                               std::set<const async_node*>& visited,
                               std::string& out) {
    if(!node || !visited.insert(node).second) {
        return;
    }

    emit_node(node, out);

    switch(node->kind) {
        case NodeKind::Task: {
            auto* task = static_cast<const standard_task*>(node);
            if(task->awaitee) {
                emit_edge(node, task->awaitee, out);
                dump_dot_walk(task->awaitee, visited, out);
            }
            break;
        }

        case NodeKind::Mutex:
        case NodeKind::Event:
        case NodeKind::Semaphore:
        case NodeKind::ConditionVariable: {
            auto* sp = static_cast<const sync_primitive*>(node);
            for(auto* w = sp->head; w != nullptr; w = w->next) {
                emit_edge(node, w, out);
                dump_dot_walk(w, visited, out);
            }
            break;
        }

        case NodeKind::MutexWaiter:
        case NodeKind::EventWaiter: {
            // Pull the owning sync_primitive into the graph.
            auto* link = static_cast<const waiter_link*>(node);
            if(link->resource) {
                dump_dot_walk(link->resource, visited, out);
            }
            break;
        }

        case NodeKind::WhenAll:
        case NodeKind::WhenAny:
        case NodeKind::Scope: {
            auto* agg = static_cast<const aggregate_op*>(node);
            for(auto* child: agg->awaitees) {
                if(child) {
                    emit_edge(node, child, out);
                    dump_dot_walk(child, visited, out);
                }
            }
            break;
        }

        case NodeKind::SystemIO: break;
    }
}

std::string async_node::dump_dot() const {
    // Walk up to find the root of the tree.
    const auto* root = this;
    while(root) {
        auto* parent = get_awaiter(root);
        if(!parent) {
            break;
        }
        root = parent;
    }

    std::string out;
    out += "digraph async_graph {\n";
    out += "  rankdir=TB;\n";
    out += "  node [fontname=\"Helvetica\", fontsize=10];\n";

    std::set<const async_node*> visited;
    dump_dot_walk(root, visited, out);

    out += "}\n";
    return out;
}

}  // namespace eventide
