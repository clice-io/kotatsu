#include "kota/async/runtime/debug.h"

#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "kota/async/runtime/walk.h"

namespace kota {

static std::string_view async_kind_name(async_node::NodeKind k) {
    switch(k) {
        case async_node::NodeKind::Task: return "Task";
        case async_node::NodeKind::Waiter: return "Waiter";
        case async_node::NodeKind::WhenAll: return "WhenAll";
        case async_node::NodeKind::WhenAny: return "WhenAny";
        case async_node::NodeKind::TaskGroup: return "TaskGroup";
        case async_node::NodeKind::SystemIO: return "SystemIO";
    }
    std::unreachable();
}

static std::string_view state_name(async_node::State s) {
    switch(s) {
        case async_node::State::Pending: return "Pending";
        case async_node::State::Running: return "Running";
        case async_node::State::Succeeded: return "Succeeded";
        case async_node::State::Failed: return "Failed";
        case async_node::State::Cancelled: return "Cancelled";
    }
    std::unreachable();
}

static std::string_view sync_kind_name(sync_primitive::Kind k) {
    switch(k) {
        case sync_primitive::Kind::Mutex: return "Mutex";
        case sync_primitive::Kind::Event: return "Event";
        case sync_primitive::Kind::Semaphore: return "Semaphore";
        case sync_primitive::Kind::ConditionVariable: return "ConditionVariable";
    }
    std::unreachable();
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

static void emit_sync_node(const sync_primitive* resource, std::string& out) {
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

struct dot_emitter : async_visitor<dot_emitter> {
    std::string out;

    const static async_node& root_of(const async_node& node) {
        const auto* root = &node;
        while(auto* parent = parent_of(*root)) {
            root = parent;
        }
        return *root;
    }

    bool visit_task(const task_frame& node) {
        return emit(node);
    }

    bool visit_wait_node(const wait_node& node) {
        return emit(node);
    }

    bool visit_aggregate(const aggregate_op& node) {
        return emit(node);
    }

    void visit_io(const io_op& node) {
        emit(node);
    }

    bool visit_sync(const sync_primitive& resource) {
        emit_sync_node(&resource, out);
        return true;
    }

    void visit_edge(const void* from, const void* to) {
        std::format_to(std::back_inserter(out),
                       R"(  {} -> {};
)",
                       node_id(from),
                       node_id(to));
    }

private:
    bool emit(const async_node& node) {
        const auto& location = location_of(node);
        const auto state = state_of(node);
        auto file = basename(location.file_name());
        std::string label;
        if(!file.empty()) {
            label = std::format(R"({}
{}
{}:{})",
                                async_kind_name(node.kind),
                                state_name(state),
                                file,
                                location.line());
        } else {
            label = std::format(R"({}
{})",
                                async_kind_name(node.kind),
                                state_name(state));
        }

        std::string_view shape = "box";
        std::string_view color = "white";
        switch(node.kind) {
            case async_node::NodeKind::Task:
                switch(state) {
                    case async_node::State::Pending: break;
                    case async_node::State::Running: color = R"("#90EE90")"; break;
                    case async_node::State::Succeeded: color = R"("#D3D3D3")"; break;
                    case async_node::State::Failed: color = R"("#FFA07A")"; break;
                    case async_node::State::Cancelled: color = R"("#FFB6C1")"; break;
                }
                break;
            case async_node::NodeKind::Waiter: color = R"("#FFDAB9")"; break;
            case async_node::NodeKind::WhenAll:
            case async_node::NodeKind::WhenAny:
            case async_node::NodeKind::TaskGroup:
                shape = "diamond";
                color = R"("#D8BFD8")";
                break;
            case async_node::NodeKind::SystemIO: color = R"("#FFFFE0")"; break;
        }

        std::format_to(std::back_inserter(out),
                       R"(  {} [label="{}", shape={}, style=filled, fillcolor={}];
)",
                       node_id(&node),
                       label,
                       shape,
                       color);
        return true;
    }
};

std::string dump_dot(const async_node& node) {
    dot_emitter emitter;
    emitter.out += R"(digraph async_graph {
)";
    emitter.out += R"(  rankdir=TB;
)";
    emitter.out += R"(  node [fontname="Helvetica", fontsize=10];
)";

    emitter.walk_node(dot_emitter::root_of(node));

    emitter.out += R"(}
)";
    return std::move(emitter.out);
}

}  // namespace kota
