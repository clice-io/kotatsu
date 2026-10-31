#pragma once

#include <string>
#include <string_view>

#include "kotatsu/cli/spec.h"
#include "kotatsu/codec/macro.h"

namespace kotatsu::zest {

/// Runtime configuration for the zest test runner.
///
/// Each field is an option of the command line, so this struct is the runner's
/// options as kotatsu::cli parses them. Downstream projects that need flags of
/// their own embed it, and one parse reads both:
///
///     struct MyTestOptions {
///         kotatsu::zest::Options zest;
///         KOTATSU_ANNOTATE(cli::meta_var = "<DIR>", description = "corpus directory")
///         <std::string> corpus_dir;
///     };
///
///     // --test-filter, --snapshot-dir and --corpus-dir in one pass.
///     auto parsed = kotatsu::cli::parse<MyTestOptions>(std::span(argv, argc).subspan(1));
///     kotatsu::zest::run_tests(std::move(parsed->zest), argc, argv);
///
/// Tests run in worker processes that re-execute the program with the original
/// arguments, so the embedding `main` sees its own flags there too.
struct Options {
    KOTATSU_ANNOTATE(cli::meta_var = "<PATTERN>",
                     description = "test name filter: SUITE or SUITE.TEST or SUITE.* or *")
    <std::string> test_filter;

    KOTATSU_ANNOTATE(description = "print all test results, not just failures")
    <bool> verbose = false;

    KOTATSU_ANNOTATE(description = "list all registered test cases and exit")
    <bool> list_tests = false;

    KOTATSU_ANNOTATE(
        cli::meta_var = "<N>",
        description = "worker processes running tests at once (0 = one per available CPU)")
    <unsigned> jobs = 0;

    KOTATSU_ANNOTATE(cli::meta_var = "<SECONDS>",
                     description =
                         "kill a test, or a worker starting or exiting, that takes longer "
                         "than this (0 = no limit)")
    <unsigned> timeout = 60;

    KOTATSU_ANNOTATE(description =
                         "run tests one after another in this process, for debuggers: a crash "
                         "or a failed ZASSERT ends the run, crash tests are skipped and "
                         "--timeout does not apply")
    <bool> no_isolation = false;

    KOTATSU_ANNOTATE(description = "update snapshot files instead of comparing")
    <bool> update_snapshots = false;

    KOTATSU_ANNOTATE(description = "remove orphaned snapshot files not used in this run")
    <bool> cleanup_snapshots = false;

    KOTATSU_ANNOTATE(cli::meta_var = "<DIR>", description = "directory for snapshot files")
    <std::string> snapshot_dir;

    // run_tests reads this from argv itself; it is declared so that a worker's
    // command line parses.
    KOTATSU_ANNOTATE(description = "internal: serve tests to the runner that started this process")
    <bool> zest_worker = false;
};

/// Parse CLI arguments and run all registered tests.
///
/// Provides a one-liner entry point for simple test executables:
///
///     int main(int argc, char** argv) {
///         return kotatsu::zest::run_cli(argc, argv);
///     }
///
/// Returns 0 once it has printed the usage for `-h` or `--help`, 2 for options
/// that do not parse, 1 for a positional filter given with `--test-filter`, and
/// otherwise what run_tests() returns.
int run_cli(int argc,
            char** argv,
            std::string_view command_overview = "unitest [options] Run unit tests");

/// Run all registered tests with explicit configuration. `argc`/`argv` are the
/// program's own; tests re-execute the program with them.
int run_tests(Options options, int argc, const char* const* argv);

}  // namespace kotatsu::zest
