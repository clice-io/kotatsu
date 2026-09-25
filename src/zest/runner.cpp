#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <format>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "execution.h"
#include "kota/deco/deco.h"
#include "kota/zest/assert/trace.h"
#include "kota/zest/runner/registry.h"
#include "kota/zest/runner/run.h"
#include "kota/zest/snapshot/snapshot.h"
#include "kota/support/glob_pattern.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <shellapi.h>
#include <windows.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif
#endif

namespace kota::zest {

namespace {

using std::chrono::milliseconds;
using std::chrono::steady_clock;

constexpr std::string_view wildcard_pattern = "*";
constexpr std::string_view green = "\033[32m";
constexpr std::string_view yellow = "\033[33m";
constexpr std::string_view red = "\033[31m";
constexpr std::string_view clear = "\033[0m";

struct CliOptions {
    Options zest;

    DecoFlag(help = "display this help and exit"; required = false; names = {"--help", "-h"})
    help = false;

    DecoInput(meta_var = "<PATTERN>"; help = "positional fallback for test name filter";
              required = false)
    <std::string> test_filter_input;
};

struct FilterPatternSet {
    kota::GlobPattern suite;
    kota::GlobPattern display;
};

struct FailedTest {
    std::string name;
    std::string path;
    std::size_t line;
    std::string reason;
};

struct RunSummary {
    std::uint32_t tests = 0;
    std::uint32_t suites = 0;
    std::uint32_t passed = 0;
    std::uint32_t failed = 0;
    std::uint32_t skipped = 0;
    milliseconds duration{0};
    std::vector<FailedTest> failed_tests;
    /// Workers that failed after their tests had all reported.
    std::vector<std::string> worker_failures;
};

/// A crash must end the test's process, not wait on a dialog nobody sees.
void silence_crash_dialogs() {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
#endif
}

/// This program's arguments after argv[0], which workers start with.
std::vector<std::string> worker_args([[maybe_unused]] int argc,
                                     [[maybe_unused]] const char* const* argv) {
#ifdef _WIN32
    // `argv` is in the ANSI code page, which may not hold every character of
    // the command line; the wide one does, and workers are spawned from UTF-8.
    int count = 0;
    auto wide = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> args;
    for(int i = 1; i < count; ++i) {
        auto size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string arg(static_cast<std::size_t>(size - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, arg.data(), size, nullptr, nullptr);
        args.push_back(std::move(arg));
    }
    LocalFree(wide);
    return args;
#else
    if(argc == 0) {
        return {};
    }
    return {argv + 1, argv + argc};
#endif
}

auto resolve_filter_patterns(std::string_view filter)
    -> std::expected<FilterPatternSet, std::string> {
    if(filter.empty()) {
        return FilterPatternSet{
            .suite = *kota::GlobPattern::create("*"),
            .display = *kota::GlobPattern::create("*"),
        };
    }

    auto dot = filter.find('.');
    if(dot == std::string_view::npos) {
        auto suite_glob = kota::GlobPattern::create(filter);
        if(!suite_glob) {
            return std::unexpected(suite_glob.error().message);
        }
        auto display_glob = kota::GlobPattern::create(std::string(filter) + ".*");
        if(!display_glob) {
            return std::unexpected(display_glob.error().message);
        }
        return FilterPatternSet{
            .suite = *std::move(suite_glob),
            .display = *std::move(display_glob),
        };
    }

    auto suite_pattern = filter.substr(0, dot);
    auto test_pattern = filter.substr(dot + 1);
    if(test_pattern.empty()) {
        test_pattern = wildcard_pattern;
    }

    auto suite_glob = kota::GlobPattern::create(suite_pattern);
    if(!suite_glob) {
        return std::unexpected(suite_glob.error().message);
    }
    auto display_str = std::format("{}.{}", suite_pattern, test_pattern);
    auto display_glob = kota::GlobPattern::create(display_str);
    if(!display_glob) {
        return std::unexpected(display_glob.error().message);
    }
    return FilterPatternSet{
        .suite = *std::move(suite_glob),
        .display = *std::move(display_glob),
    };
}

bool matches_filter(const Entry& entry, const FilterPatternSet& patterns) {
    if(!patterns.suite.match(entry.suite)) {
        return false;
    }
    return patterns.display.is_trivial_match_all() || patterns.display.match(entry.name);
}

auto collect_entries(std::span<const TestSuite> suites) -> std::vector<Entry> {
    std::vector<Entry> entries;
    for(const auto& suite: suites) {
        for(auto& test_case: suite.cases()) {
            auto name = std::format("{}.{}", suite.name, test_case.name);
            entries.push_back(Entry{
                .suite = suite.name,
                .name = std::move(name),
                .test_case = std::move(test_case),
            });
        }
    }
    // Registration follows static initialization, whose order is unspecified.
    // Cases of one TEST_CASE_GROUP share a line and keep the order they were
    // added in.
    std::ranges::stable_sort(entries, {}, [](const Entry& entry) {
        return std::tie(entry.suite, entry.test_case.path, entry.test_case.line);
    });
    return entries;
}

void print_output(std::string_view output) {
    while(output.ends_with('\n') || output.ends_with('\r')) {
        output.remove_suffix(1);
    }
    if(!output.empty()) {
        std::println("{}", output);
    }
}

/// Prints and tallies outcomes as they arrive.
struct Reporter {
    RunSummary& summary;
    bool verbose;

    void record(const Entry& entry, const Outcome& outcome) {
        switch(outcome.verdict) {
            case Verdict::Passed:
                summary.passed += 1;
                if(verbose) {
                    print_output(outcome.output);
                    std::println("{}[       OK ] {} ({} ms){}",
                                 green,
                                 entry.name,
                                 outcome.duration.count(),
                                 clear);
                }
                return;
            case Verdict::Skipped:
                summary.skipped += 1;
                if(verbose) {
                    print_output(outcome.output);
                    std::println("{}[ SKIPPED  ] {}{}", yellow, entry.name, clear);
                }
                return;
            case Verdict::Failed: fail(entry, outcome, "FAILED", ""); return;
            case Verdict::Crashed:
                fail(entry, outcome, "CRASHED", std::format("crashed: {}", outcome.detail));
                return;
            case Verdict::TimedOut: fail(entry, outcome, "TIMEOUT", "timed out"); return;
        }
    }

    void fail(const Entry& entry,
              const Outcome& outcome,
              std::string_view label,
              std::string reason) {
        print_output(outcome.output);
        std::println("{}[{:>9} ] {} ({} ms){}",
                     red,
                     label,
                     entry.name,
                     outcome.duration.count(),
                     clear);
        if(!outcome.detail.empty()) {
            std::println("{}             {}{}", red, outcome.detail, clear);
        }
        summary.failed += 1;
        summary.failed_tests.push_back(FailedTest{
            .name = entry.name,
            .path = entry.test_case.path,
            .line = entry.test_case.line,
            .reason = std::move(reason),
        });
    }
};

void print_summary(const RunSummary& summary) {
    std::println("{}[----------] Global test environment tear-down. {}", green, clear);
    std::println("{}[==========] {} tests from {} test suites ran. ({} ms total){}",
                 green,
                 summary.tests,
                 summary.suites,
                 summary.duration.count(),
                 clear);

    if(summary.passed > 0) {
        std::println("{}[  PASSED  ] {} tests.{}", green, summary.passed, clear);
    }
    if(summary.skipped > 0) {
        std::println("{}[  SKIPPED ] {} tests.{}", yellow, summary.skipped, clear);
    }
    if(summary.failed > 0) {
        std::println("{}[  FAILED  ] {} tests, listed below:{}", red, summary.failed, clear);
        for(const auto& failed: summary.failed_tests) {
            if(failed.reason.empty()) {
                std::println("{}[  FAILED  ] {}{}", red, failed.name, clear);
            } else {
                std::println("{}[  FAILED  ] {} ({}){}", red, failed.name, failed.reason, clear);
            }
            std::println("             at {}:{}", failed.path, failed.line);
        }
        std::println("{}{} FAILED TEST{}{}",
                     red,
                     summary.failed,
                     summary.failed == 1 ? "" : "S",
                     clear);
    }
    for(const auto& failure: summary.worker_failures) {
        std::println("{}[  FAILED  ] {}{}", red, failure, clear);
    }
}

}  // namespace

Verdict verdict_of(TestState state) {
    switch(state) {
        case TestState::Passed: return Verdict::Passed;
        case TestState::Skipped: return Verdict::Skipped;
        case TestState::Failed: return Verdict::Failed;
    }
    std::unreachable();
}

TestState run_in_process(const Entry& entry) {
    auto& state = current_test_state();
    state = TestState::Passed;
#ifdef __cpp_exceptions
    if(trace_exception([&] { entry.test_case.test(); }, true)) {
        failure();
    }
#else
    entry.test_case.test();
#endif
    return state;
}

int run_cli(int argc, char** argv, std::string_view command_overview) {
    auto args = kota::deco::util::argvify(argc, argv);
    auto renderer = kota::deco::cli::text::ModernRenderer();
    kota::deco::cli::Command<CliOptions> command(command_overview);
    command.render_with(renderer);
    command.after<&CliOptions::help>([](auto& step) {
        step.print_usage();
        return step.stop();
    });

    auto parsed = command.invoke(args);
    if(!parsed.has_value()) {
        std::println(stderr, "Error parsing options: {}", parsed.error().message);
        return 1;
    }

    auto& cli = parsed->options;
    if(cli.help.has_value() && *cli.help) {
        return 0;
    }

    if(cli.test_filter_input.has_value() && !cli.zest.test_filter->empty()) {
        std::println(stderr, "Error: cannot use both positional filter and --test-filter");
        return 1;
    }

    if(cli.test_filter_input.has_value()) {
        cli.zest.test_filter = std::move(*cli.test_filter_input);
    }

    return run_tests(std::move(cli.zest), argc, argv);
}

int run_tests(Options options, int argc, const char* const* argv) {
    return Runner::instance().run_tests(std::move(options), argc, argv);
}

Runner& Runner::instance() {
    static Runner runner;
    return runner;
}

void Runner::add_suite(std::string_view name, std::vector<TestCase> (*cases)()) {
    suites.emplace_back(std::string(name), cases);
}

int Runner::run_tests(Options options, int argc, const char* const* argv) {
    silence_crash_dialogs();
    set_update_snapshots(*options.update_snapshots);
    set_snapshot_dir(*options.snapshot_dir);

    auto entries = collect_entries(suites);
    // Read from argv rather than `options`, which the embedding program may
    // have built itself: a worker that took itself for a runner would start
    // workers of its own.
    auto args = std::span(argv, static_cast<std::size_t>(argc));
    if(std::ranges::any_of(args,
                           [](std::string_view arg) { return arg == protocol::worker_flag; })) {
        serve(entries);
        return 0;
    }

    // Workers find tests by name.
    std::unordered_set<std::string_view> names;
    for(const auto& entry: entries) {
        if(!names.insert(entry.name).second) {
            std::println("{}Error: more than one test is named {}{}", red, entry.name, clear);
            return 1;
        }
    }

    auto patterns = resolve_filter_patterns(*options.test_filter);
    if(!patterns) {
        std::println("{}Error: invalid filter pattern: {}{}", red, patterns.error(), clear);
        return 1;
    }

    std::vector<const Entry*> matched;
    for(const auto& entry: entries) {
        if(matches_filter(entry, *patterns)) {
            matched.push_back(&entry);
        }
    }

    if(*options.list_tests) {
        for(const auto* entry: matched) {
            std::println("{}", entry->name);
        }
        return 0;
    }

    const bool verbose = *options.verbose;
    const bool focus_mode = std::ranges::any_of(matched, [](const Entry* entry) {
        return entry->test_case.attrs.focus && !entry->test_case.attrs.skip;
    });

    RunSummary summary;
    Reporter reporter{.summary = summary, .verbose = verbose};
    std::vector<const Entry*> runnable;
    std::unordered_set<std::string_view> active_suites;
    for(const auto* entry: matched) {
        const auto& attrs = entry->test_case.attrs;
        if(focus_mode && !attrs.focus) {
            summary.skipped += 1;
            continue;
        }
        if(attrs.skip) {
            reporter.record(*entry, Outcome{.verdict = Verdict::Skipped, .duration = {}});
            continue;
        }
        active_suites.insert(entry->suite);
        runnable.push_back(entry);
    }
    summary.suites = static_cast<std::uint32_t>(active_suites.size());
    summary.tests = static_cast<std::uint32_t>(runnable.size());

    std::println("{}[----------] Global test environment set-up.{}", green, clear);
    if(focus_mode) {
        std::println("{}[  FOCUS   ] Running in focus-only mode.{}", yellow, clear);
    }

    auto begin = steady_clock::now();
    if(*options.no_isolation) {
        for(const auto* entry: runnable) {
            if(verbose) {
                std::println("{}[ RUN      ] {}{}", green, entry->name, clear);
            }
            auto test_begin = steady_clock::now();
            auto verdict = verdict_of(run_in_process(*entry));
            reporter.record(*entry,
                            Outcome{.verdict = verdict, .duration = elapsed_since(test_begin)});
        }
    } else {
        auto jobs = *options.jobs;
        if(jobs == 0) {
            jobs = std::max(1u, std::thread::hardware_concurrency());
        }
        PoolOptions pool{
            .args = worker_args(argc, argv),
            .jobs = jobs,
            .timeout = std::chrono::seconds(*options.timeout),
        };
        auto report = [&](const Entry& entry, const Outcome& outcome) {
            reporter.record(entry, outcome);
        };
        auto ran = run_pool(runnable, pool, report);
        if(!ran) {
            print_output(ran.error().output);
            std::println("{}Error: {}{}", red, ran.error().detail, clear);
            return 1;
        }
        for(auto& failure: *ran) {
            print_output(failure.output);
            std::println("{}[   WORKER ] {}{}", red, failure.detail, clear);
            summary.worker_failures.push_back(std::move(failure.detail));
        }
    }
    summary.duration = elapsed_since(begin);

    if(*options.cleanup_snapshots) {
        // A test that failed early never checked its snapshots; they only
        // look orphaned.
        if(summary.failed != 0) {
            std::println("[snapshot] cleanup skipped: some tests failed");
        } else if(auto removed = cleanup_unused_snapshots(); removed > 0) {
            std::println("[snapshot] cleaned up {} orphaned file{}",
                         removed,
                         removed == 1 ? "" : "s");
        }
    }

    // Workers finish in any order; the list reads the same every run.
    std::ranges::sort(summary.failed_tests, {}, &FailedTest::name);
    print_summary(summary);
    return summary.failed != 0 || !summary.worker_failures.empty();
}

}  // namespace kota::zest
