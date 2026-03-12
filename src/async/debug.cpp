#include <format>
#include <set>
#include <string>
#include <string_view>

#include "eventide/async/frame.h"
#include "eventide/async/sync.h"

namespace eventide {

static std::string_view async_kind_name(async_node::NodeKind k) {
    switch(k) {
        case async_node::NodeKind::Task: return "Task";
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
        case async_node::Failed: return "Failed";
    }
    return "Unknown";
}

static std::string_view sync_kind_name(sync_primitive::Kind k) {
    switch(k) {
        case sync_primitive::Kind::Mutex: return "Mutex";
        case sync_primitive::Kind::Event: return "Event";
        case sync_primitive::Kind::Semaphore: return "Semaphore";
        case sync_primitive::Kind::ConditionVariable: return "ConditionVariable";
    }
    return "Unknown";
}

static std::string node_id(const void* node) {
    return std::format("n{:x}", reinterpret_cast<std::uintptr_t>(node));
}

static std::string_view basename(const char* path) {
    if(!path || path[0] == '\0') {
        return {};
    }
    std::string_view sv(path);
    auto pos = sv.find_last_of(R"(/\)");
    return pos != std::string_view::npos ? sv.substr(pos + 1) : sv;
}

static void emit_node(const async_node* node, std::string& out) {
    auto file = basename(node->location.file_name());
    std::string label;
    if(!file.empty()) {
        label = std::format(R"({}
{}
{}:{})",
                            async_kind_name(node->kind),
                            state_name(node->state),
                            file,
                            node->location.line());
    } else {
        label = std::format(R"({}
{})",
                            async_kind_name(node->kind),
                            state_name(node->state));
    }

    std::string_view shape = "box";
    std::string_view color = "white";

    if(node->is_standard_task()) {
        switch(node->state) {
            case async_node::Running: color = R"("#90EE90")"; break;
            case async_node::Finished: color = R"("#D3D3D3")"; break;
            case async_node::Cancelled: color = R"("#FFB6C1")"; break;
            case async_node::Failed: color = R"("#FFA07A")"; break;
            default: break;
        }
    } else if(node->is_aggregate_op()) {
        shape = "diamond";
        color = R"("#D8BFD8")";
    } else if(node->kind == async_node::NodeKind::SystemIO) {
        color = R"("#FFFFE0")";
    } else if(node->is_waiter_link()) {
        color = R"("#FFDAB9")";
    }

    std::format_to(std::back_inserter(out),
                   R"(  {} [label="{}", shape={}, style=filled, fillcolor={}];
)",
                   node_id(node),
                   label,
                   shape,
                   color);
}

static void emit_node(const sync_primitive* resource, std::string& out) {
    auto file = basename(resource->location.file_name());
    std::string label;
    if(!file.empty()) {
        label = std::format(R"({}
{}:{})",
                            sync_kind_name(resource->kind),
                            file,
                            resource->location.line());
    } else {
        label = std::format("{}", sync_kind_name(resource->kind));
    }

    std::format_to(std::back_inserter(out),
                   R"(  {} [label="{}", shape=ellipse, style=filled, fillcolor="{}"];
)",
                   node_id(resource),
                   label,
                   "#ADD8E6");
}

static void emit_edge(const void* from, const void* to, std::string& out) {
    std::format_to(std::back_inserter(out),
                   R"(  {} -> {};
)",
                   node_id(from),
                   node_id(to));
}

const sync_primitive* async_node::get_resource_parent(const async_node* node) {
    switch(node->kind) {
        case NodeKind::MutexWaiter:
        case NodeKind::EventWaiter: return static_cast<const waiter_link*>(node)->resource;
        default: return nullptr;
    }
}

/// Returns the awaiter (parent) of a node, or nullptr for roots.
const async_node* async_node::get_awaiter(const async_node* node) {
    switch(node->kind) {
        case NodeKind::Task: return static_cast<const standard_task*>(node)->awaiter;
        case NodeKind::MutexWaiter:
        case NodeKind::EventWaiter: {
            auto* link = static_cast<const waiter_link*>(node);
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
                               std::set<const void*>& visited,
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

        case NodeKind::MutexWaiter:
        case NodeKind::EventWaiter: {
            auto* link = static_cast<const waiter_link*>(node);
            if(link->resource) {
                emit_edge(node, link->resource, out);
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

void async_node::dump_dot_walk(const sync_primitive* resource,
                               std::set<const void*>& visited,
                               std::string& out) {
    if(!resource || !visited.insert(resource).second) {
        return;
    }

    emit_node(resource, out);
    for(auto* waiter = resource->head; waiter != nullptr; waiter = waiter->next) {
        emit_edge(resource, waiter, out);
        dump_dot_walk(waiter, visited, out);
    }
}

std::string async_node::dump_dot() const {
    // Walk up to find the root of the graph.
    const auto* async_root = this;
    const sync_primitive* resource_root = nullptr;
    while(async_root) {
        if(auto* resource = get_resource_parent(async_root)) {
            resource_root = resource;
            break;
        }

        auto* parent = get_awaiter(async_root);
        if(!parent) {
            break;
        }
        async_root = parent;
    }

    std::string out;
    out += R"(digraph async_graph {
)";
    out += R"(  rankdir=TB;
)";
    out += R"(  node [fontname="Helvetica", fontsize=10];
)";

    std::set<const void*> visited;
    if(resource_root) {
        dump_dot_walk(resource_root, visited, out);
    } else {
        dump_dot_walk(async_root, visited, out);
    }

    out += R"(}
)";
    return out;
}

}  // namespace eventide
