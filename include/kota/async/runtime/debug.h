#pragma once

#include <string>

#include "kota/async/runtime/node.h"
#include "kota/async/runtime/task.h"

namespace kota {

/// Draws, in Graphviz dot, the task tree `node` belongs to: from its root down
/// to the sync primitives its waiters wait on.
std::string dump_dot(const async_node& node);

template <typename T, typename E, typename C>
std::string dump_dot(task<T, E, C>& t) {
    return dump_dot(detail::task_access::promise(t));
}

}  // namespace kota
