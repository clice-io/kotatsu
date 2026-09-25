#pragma once

#include <string>
#include <string_view>

#include "kota/deco/deco.h"

namespace kota::zest {

/// Runtime configuration for the zest test runner.
///
/// Fields use kota::deco macros, so this struct doubles as a CLI option definition.
/// Downstream projects that need custom flags can embed it and let deco's recursive
/// parsing handle both sets transparently:
///
///     struct MyTestOptions {
///         kota::zest::Options zest;
///         DecoKVStyled(kota::deco::decl::KVStyle::JoinedOrSeparate,
///                      meta_var = "<DIR>"; help = "corpus directory")
///         <std::string> corpus_dir;
///     };
///
///     // deco parses --test-filter, --snapshot-dir, AND --corpus-dir in one pass.
///     auto parsed = kota::deco::cli::parse<MyTestOptions>(args, renderer);
///     kota::zest::run_tests(std::move(parsed->options.zest), argc, argv);
///
/// Tests run in worker processes that re-execute the program with the original
/// arguments, so the embedding `main` sees its own flags there too.
struct Options {
    DecoKVStyled(kota::deco::decl::KVStyle::JoinedOrSeparate, meta_var = "<PATTERN>";
                 help = "test name filter: SUITE or SUITE.TEST or SUITE.* or *";
                 required = false)
    <std::string> test_filter = "";

    DecoFlag(help = "print all test results, not just failures"; required = false)
    verbose = false;

    DecoFlag(help = "list all registered test cases and exit"; required = false)
    list_tests = false;

    DecoKVStyled(kota::deco::decl::KVStyle::JoinedOrSeparate, meta_var = "<N>";
                 help = "worker processes running tests at once (0 = one per CPU)";
                 required = false)
    <unsigned> jobs = 0;

    DecoKVStyled(kota::deco::decl::KVStyle::JoinedOrSeparate, meta_var = "<SECONDS>";
                 help =
                     "kill a test, or a worker starting or exiting, that takes longer "
                     "than this (0 = no limit)";
                 required = false)
    <unsigned> timeout = 60;

    DecoFlag(help =
                 "run tests one after another in this process, for debuggers: a crash "
                 "ends the run and --timeout does not apply";
             required = false)
    no_isolation = false;

    DecoFlag(help = "update snapshot files instead of comparing"; required = false)
    update_snapshots = false;

    DecoFlag(help = "remove orphaned snapshot files not used in this run"; required = false)
    cleanup_snapshots = false;

    DecoKVStyled(kota::deco::decl::KVStyle::JoinedOrSeparate, meta_var = "<DIR>";
                 help = "directory for snapshot files";
                 required = false)
    <std::string> snapshot_dir = "";

    // run_tests reads this from argv itself; it is declared so that a worker's
    // command line parses.
    DecoFlag(help = "internal: serve tests to the runner that started this process";
             required = false)
    zest_worker = false;
};

/// Parse CLI arguments and run all registered tests.
///
/// Provides a one-liner entry point for simple test executables:
///
///     int main(int argc, char** argv) {
///         return kota::zest::run_cli(argc, argv);
///     }
///
int run_cli(int argc,
            char** argv,
            std::string_view command_overview = "unitest [options] Run unit tests");

/// Run all registered tests with explicit configuration. `argc`/`argv` are the
/// program's own; tests re-execute the program with them.
int run_tests(Options options, int argc, const char* const* argv);

}  // namespace kota::zest
