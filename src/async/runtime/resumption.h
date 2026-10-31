#pragma once

#include <coroutine>
#include <vector>

namespace kotatsu::async {

class aggregate_op;

}  // namespace kotatsu::async

namespace kotatsu::async::detail {

/// Runs the code it scopes as part of the resumption under way, or as one of
/// its own: a task resumed meanwhile, by a cancel say, leaves what sync
/// primitives woke queued, for the loop to resume once whatever runs has
/// suspended, rather than resuming it inside the scope. The frames of the
/// tasks that ended inside the outermost scope are destroyed as it ends.
class ResumptionScope {
public:
    ResumptionScope() noexcept;

    ResumptionScope(const ResumptionScope&) = delete;
    ResumptionScope& operator=(const ResumptionScope&) = delete;

    ~ResumptionScope();

private:
    bool outermost;
};

/// Sets aside the resumption under way on this thread, if any, while an
/// event loop runs inside it or is destroyed there: the loop's resumptions are
/// outermost, so the frames that end in them go before the loop does, and
/// those that ended in the outer resumption wait for it.
class LoopRunScope {
public:
    LoopRunScope() noexcept;

    LoopRunScope(const LoopRunScope&) = delete;
    LoopRunScope& operator=(const LoopRunScope&) = delete;

    ~LoopRunScope();

private:
    bool outer_resuming;
    std::vector<std::coroutine_handle<>> outer_roots;
    aggregate_op* outer_groups;
};

}  // namespace kotatsu::async::detail
