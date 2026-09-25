#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "kota/zest/runner/registry.h"
#include "kota/support/functional.h"

namespace kota::zest {

/// A registered test case together with the suite it belongs to.
struct Entry {
    std::string suite;
    /// `SUITE.TEST`, the name filters, reports and workers use.
    std::string name;
    TestCase test_case;
};

enum class Verdict : std::uint8_t {
    Passed,
    Skipped,
    Failed,
    /// The worker died before the test finished.
    Crashed,
    /// The test outlived --timeout and its worker was killed.
    TimedOut,
};

struct Outcome {
    Verdict verdict;
    std::chrono::milliseconds duration;
    /// What the test printed, when a worker ran it.
    std::string output = {};
    /// How the worker died, for a crash.
    std::string detail = {};
};

Verdict verdict_of(TestState state);

inline std::chrono::milliseconds elapsed_since(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 begin);
}

/// Runs one test in this process and returns its state.
TestState run_in_process(const Entry& entry);

/// Lines a runner and its workers exchange over the worker's stdin:
///
///     worker -> runner   ready              (once, when it can take tests)
///     runner -> worker   run SUITE.TEST
///     worker -> runner   snapshot PATH      (once per snapshot file checked)
///     worker -> runner   done passed|skipped|failed
///
/// The runner hangs up once it has no more tests, and the worker exits.
namespace protocol {

constexpr std::string_view ready = "ready";
constexpr std::string_view run = "run ";
constexpr std::string_view snapshot = "snapshot ";
constexpr std::string_view done = "done ";

std::string_view state_name(TestState state);

std::optional<TestState> parse_state(std::string_view name);

/// Removes the first complete line from `pending` and returns it without its
/// newline, or nothing while no line is complete.
std::optional<std::string> take_line(std::string& pending);

}  // namespace protocol

/// The worker side: runs the tests the runner names until it hangs up.
int serve(std::span<const Entry> entries);

/// A worker that went wrong outside any test: it could not start, or it ended
/// badly after its last test, as a leak check or a static destructor can make it.
struct WorkerFailure {
    std::string detail;
    /// Everything the worker printed.
    std::string output;
};

struct PoolOptions {
    /// This program's arguments without argv[0]; workers start with them.
    std::vector<std::string> args;
    unsigned jobs;
    /// Zero means no limit.
    std::chrono::milliseconds timeout;
};

/// The runner side: runs `tests` on worker processes and reports each outcome
/// as it arrives. Returns the workers that ended badly after their tests, or
/// the failure that stopped the run when a worker could not start.
std::expected<std::vector<WorkerFailure>, WorkerFailure>
    run_pool(std::span<const Entry* const> tests,
             const PoolOptions& options,
             function_ref<void(const Entry&, const Outcome&)> report);

}  // namespace kota::zest
