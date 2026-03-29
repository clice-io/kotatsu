#pragma once

#include <string>
#include <string_view>

namespace eventide::zest {

/// Runtime configuration for executing registered zest test cases.
struct RunnerOptions {
    /// Test filter in the form SUITE or SUITE.TEST, with '*' supported as a wildcard.
    std::string filter;
    /// When true, per-test output is limited to failing cases; the final summary is still printed.
    bool only_failed_output = false;
    /// When true, test cases are executed in parallel across multiple threads.
    bool parallel = false;
};

/// Parse CLI arguments into RunnerOptions and execute registered tests.
int run_cli(int argc,
            char** argv,
            std::string_view command_overview = "unitest [options] Run unit tests");

/// Execute all registered tests using an explicit runtime configuration.
int run_tests(RunnerOptions options);

/// Convenience overload for running with only a test-name filter.
int run_tests(std::string_view filter);

}  // namespace eventide::zest
