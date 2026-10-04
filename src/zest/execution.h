#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <mutex>
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
    /// A crash test finished instead of crashing.
    Survived,
};

struct Outcome {
    Verdict verdict;
    std::chrono::milliseconds duration;
    /// What the test printed, when a worker ran it.
    std::string output = {};
    /// A line the report adds under the test's status: how a crash or a crash
    /// test ended its worker, why the test was skipped, or what went wrong
    /// after a failed ZASSERT.
    std::string detail = {};
};

Verdict verdict_of(TestState state);

inline std::chrono::milliseconds elapsed_since(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 begin);
}

/// Runs one test in this process and returns its state.
TestState run_in_process(const Entry& entry);

/// Tells whoever runs this process's tests that a failed ZASSERT is ending it,
/// before the fatal hooks run: the runner a worker serves, or the report of a
/// run without isolation. Set while tests run, and null otherwise.
inline std::atomic<void (*)()> fatal_notice = nullptr;

/// Tells the runner a worker serves of a snapshot file the moment the test
/// checks it, so that a crash test that passes has its snapshots counted.
inline std::atomic<void (*)(std::string_view path)> snapshot_notice = nullptr;

/// Whether a failed check ends the process as a failed ZASSERT does: in a
/// crash test, whose worker could not report the failure once it crashed.
inline std::atomic<bool> failures_are_fatal = false;

/// Held for each line a worker sends the runner, and for good from the moment
/// a failed ZASSERT begins ending the process, so that nothing replies for its
/// test meanwhile. Recursive: the thread ending the process still sends.
std::recursive_mutex& reply_mutex();

/// What a process exits with once a failed ZASSERT has run its hooks: a code
/// abort() and the sanitizers do not use.
constexpr int fatal_exit_code = 86;

/// Hands everything printed so far to the output file or terminal.
void flush_output();

/// Lines a runner and its workers exchange over the worker's stdin:
///
///     worker -> runner   ready              (once, when it can take tests)
///     runner -> worker   run SUITE.TEST
///     worker -> runner   snapshot PATH      (as the test checks a snapshot file)
///     worker -> runner   done passed|skipped|failed
///                        or fatal           (a failed ZASSERT: the test
///                                            failed, and the worker exits)
///
/// The runner hangs up once it has no more tests, and the worker exits.
namespace protocol {

/// The flag a worker is started with.
constexpr std::string_view worker_flag = "--zest-worker";

constexpr std::string_view ready = "ready";
constexpr std::string_view run = "run ";
constexpr std::string_view snapshot = "snapshot ";
constexpr std::string_view fatal = "fatal";
constexpr std::string_view done = "done ";

std::string_view state_name(TestState state);

std::optional<TestState> parse_state(std::string_view name);

}  // namespace protocol

/// Counts `path` as checked, as if this process had checked it.
void record_snapshot_access(std::string_view path);

/// The worker side: runs the tests the runner names until it hangs up.
void serve(std::span<const Entry> entries);

/// A worker that went wrong outside any test: it could not start, or it ended
/// badly after its last test, as a leak check or a static destructor can make it.
struct WorkerFailure {
    std::string detail;
    /// What the worker printed.
    std::string output = {};
};

struct PoolOptions {
    /// This program's argv, argv[0] included: workers start with it, so a
    /// program that reads its own name sees the same one there.
    std::vector<std::string> args;
    /// Zero means one per CPU this process may use.
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
