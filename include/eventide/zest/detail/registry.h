#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "eventide/zest/run.h"

namespace eventide::zest {

enum class TestState {
    Passed,
    Skipped,
    Failed,
    Fatal,
};

struct TestAttrs {
    bool skip = false;
    bool focus = false;
};

struct TestCase {
    std::string name;
    std::string path;
    std::size_t line;
    TestAttrs attrs;
    std::function<TestState()> test;
};

struct TestSuite {
    std::string name;
    std::vector<TestCase> (*cases)();
};

inline TestState& current_test_state() {
    thread_local TestState state = TestState::Passed;
    return state;
}

inline void failure() {
    current_test_state() = TestState::Failed;
}

inline void pass() {
    current_test_state() = TestState::Passed;
}

inline void skip() {
    current_test_state() = TestState::Skipped;
}

class Runner {
public:
    static Runner& instance();

    void add_suite(std::string_view suite, std::vector<TestCase> (*cases)());

    int run_tests(RunnerOptions options);
    int run_tests(std::string_view filter);

private:
    std::vector<TestSuite> suites;
};

}  // namespace eventide::zest
