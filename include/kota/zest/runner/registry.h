#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "kota/zest/runner/run.h"

namespace kota::zest {

enum class TestState : std::uint8_t {
    Passed,
    Skipped,
    Failed,
};

struct TestAttrs {
    bool skip = false;
    /// When any selected test is focused, only focused tests run.
    bool focus = false;
    /// Runs while no other test runs. Tests running at once never share a
    /// process, so this is for contention outside it: fixed file or pipe
    /// names, or timing that load disturbs.
    bool serial = false;
};

struct TestCase {
    std::string name;
    std::string path;
    std::size_t line;
    TestAttrs attrs;
    std::function<void()> test;
};

struct TestSuite {
    std::string name;
    std::vector<TestCase> (*cases)();
};

/// State of the running test. A process runs one test at a time, so a check
/// that fails on a thread the test started still fails the test.
inline std::atomic<TestState>& current_test_state() {
    static std::atomic<TestState> state = TestState::Passed;
    return state;
}

inline void failure() {
    current_test_state() = TestState::Failed;
}

/// Marks the running test skipped, unless one of its checks already failed.
inline void skip() {
    auto state = TestState::Passed;
    current_test_state().compare_exchange_strong(state, TestState::Skipped);
}

class Runner {
public:
    static Runner& instance();

    void add_suite(std::string_view suite, std::vector<TestCase> (*cases)());

    int run_tests(Options options, int argc, const char* const* argv);

private:
    std::vector<TestSuite> suites;
};

}  // namespace kota::zest
