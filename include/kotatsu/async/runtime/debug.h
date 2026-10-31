#pragma once

#include <string>

#include "kotatsu/async/runtime/node.h"
#include "kotatsu/async/runtime/task.h"

namespace kotatsu::async {

/// Draws, in Graphviz dot, the task tree `node` belongs to: from its root down
/// to the sync primitives its waiters wait on.
std::string dump_dot(const async_node& node);

template <typename T, typename E, typename C>
std::string dump_dot(task<T, E, C>& t) {
    return dump_dot(detail::task_access::promise(t));
}

}  // namespace kotatsu::async
