#pragma once

namespace kota::detail {

/// Runs the code it scopes as part of the resumption under way, or as one of
/// its own: a task resumed meanwhile, by a cancel say, leaves what sync
/// primitives woke queued, for the loop to resume once whatever runs has
/// suspended, rather than resuming it inside the scope.
class ResumptionScope {
public:
    ResumptionScope() noexcept;

    ResumptionScope(const ResumptionScope&) = delete;
    ResumptionScope& operator=(const ResumptionScope&) = delete;

    ~ResumptionScope();

private:
    bool outermost;
};

}  // namespace kota::detail
