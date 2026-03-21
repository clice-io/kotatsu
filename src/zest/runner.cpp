#include <chrono>
#include <expected>
#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "eventide/deco/decl.h"
#include "eventide/deco/macro.h"
#include "eventide/deco/runtime.h"
#include "eventide/zest/detail/registry.h"
#include "eventide/zest/run.h"

namespace {

constexpr std::string_view wildcard_pattern = "*";
constexpr std::string_view green = "\033[32m";
constexpr std::string_view yellow = "\033[33m";
constexpr std::string_view red = "\033[31m";
constexpr std::string_view clear = "\033[0m";

struct ZestCliOptions {
    DecoKVStyled(style = deco::decl::KVStyle::Joined | deco::decl::KVStyle::Separate,
                 names = {"--test-filter"};
                 meta_var = "<PATTERN>";
                 help = "test name filters, SUITE or SUITE.TEST or SUITE.* or *";
                 required = false)
    <std::string> test_filter = "";

    DecoInput(meta_var = "<PATTERN>"; help = "positional fallback for test name filters";
              required = false)
    <std::string> test_filter_input;

    DecoFlag(names = {"--only-failed"}; help = "Only print failed test cases"; required = false)
    only_failed = false;
};

auto to_runner_options(ZestCliOptions options)
    -> std::expected<eventide::zest::RunnerOptions, std::string> {
    if(options.test_filter_input.value.has_value() && !options.test_filter->empty()) {
        return std::unexpected("cannot use both positional filter and --test-filter");
    }

    eventide::zest::RunnerOptions runner_options;
    runner_options.only_failed_output = *options.only_failed;
    if(options.test_filter_input.value.has_value()) {
        runner_options.filter = std::move(*options.test_filter_input);
    } else {
        runner_options.filter = std::move(*options.test_filter);
    }
    return runner_options;
}

struct FilterPatternSet {
    std::string suite = std::string(wildcard_pattern);
    std::string display = std::string(wildcard_pattern);
};

struct FailedTest {
    std::string name;
    std::string path;
    std::size_t line;
};

struct RunSummary {
    std::uint32_t tests = 0;
    std::uint32_t suites = 0;
    std::uint32_t failed = 0;
    std::uint32_t skipped = 0;
    std::chrono::milliseconds duration{0};
    std::vector<FailedTest> failed_tests;
};

using SuiteMap = std::unordered_map<std::string, std::vector<eventide::zest::TestCase>>;

bool matches_pattern(std::string_view text, std::string_view pattern) {
    std::size_t ti = 0, pi = 0, star = std::string_view::npos, match = 0;
    while(ti < text.size()) {
        if(pi < pattern.size() && (pattern[pi] == text[ti])) {
            ++ti;
            ++pi;
        } else if(pi < pattern.size() && pattern[pi] == '*') {
            star = pi++;
            match = ti;
        } else if(star != std::string_view::npos) {
            pi = star + 1;
            ti = ++match;
        } else {
            return false;
        }
    }
    while(pi < pattern.size() && pattern[pi] == '*') {
        ++pi;
    }
    return pi == pattern.size();
}

auto make_display_name(std::string_view suite_name, std::string_view test_name) -> std::string {
    return std::format("{}.{}", suite_name, test_name);
}

auto resolve_filter_patterns(std::string_view filter) -> FilterPatternSet {
    FilterPatternSet patterns;
    if(filter.empty()) {
        return patterns;
    }

    auto dot = filter.find('.');
    if(dot == std::string_view::npos) {
        patterns.suite.assign(filter);
        patterns.display = patterns.suite + ".*";
        return patterns;
    }

    patterns.suite.assign(filter.substr(0, dot));
    auto test_pattern = filter.substr(dot + 1);
    if(test_pattern.empty()) {
        test_pattern = wildcard_pattern;
    }

    patterns.display.assign(filter.substr(0, dot));
    patterns.display.push_back('.');
    patterns.display.append(test_pattern);
    return patterns;
}

auto group_suites(const std::vector<eventide::zest::TestSuite>& suites) -> SuiteMap {
    SuiteMap grouped_suites;
    for(const auto& suite: suites) {
        auto& target = grouped_suites[suite.name];
        auto cases = suite.cases();
        for(auto& test_case: cases) {
            target.emplace_back(std::move(test_case));
        }
    }
    return grouped_suites;
}

bool matches_suite_filter(std::string_view suite_name, const FilterPatternSet& patterns) {
    return matches_pattern(suite_name, patterns.suite);
}

bool matches_test_filter(std::string_view suite_name,
                         std::string_view test_name,
                         const FilterPatternSet& patterns) {
    if(patterns.display == wildcard_pattern) {
        return true;
    }
    return matches_pattern(make_display_name(suite_name, test_name), patterns.display);
}

bool has_focused_tests(const SuiteMap& grouped_suites, const FilterPatternSet& patterns) {
    for(const auto& [suite_name, test_cases]: grouped_suites) {
        if(!matches_suite_filter(suite_name, patterns)) {
            continue;
        }

        for(const auto& test_case: test_cases) {
            if(matches_test_filter(suite_name, test_case.name, patterns) && test_case.attrs.focus &&
               !test_case.attrs.skip) {
                return true;
            }
        }
    }
    return false;
}

bool is_failure(eventide::zest::TestState state) {
    return state == eventide::zest::TestState::Failed || state == eventide::zest::TestState::Fatal;
}

void print_run_result(std::string_view display_name,
                      bool failed,
                      std::chrono::milliseconds duration,
                      bool only_failed_output) {
    if(failed || !only_failed_output) {
        std::println("{0}[   {1} ] {2} ({3} ms){4}",
                     failed ? red : green,
                     failed ? "FAILED" : "    OK",
                     display_name,
                     duration.count(),
                     clear);
    }
}

void print_summary(const RunSummary& summary) {
    std::println("{}[----------] Global test environment tear-down. {}", green, clear);
    std::println("{}[==========] {} tests from {} test suites ran. ({} ms total){}",
                 green,
                 summary.tests,
                 summary.suites,
                 summary.duration.count(),
                 clear);

    const auto passed = summary.tests - summary.failed;
    if(passed > 0) {
        std::println("{}[  PASSED  ] {} tests.{}", green, passed, clear);
    }
    if(summary.skipped > 0) {
        std::println("{}[  SKIPPED ] {} tests.{}", yellow, summary.skipped, clear);
    }
    if(summary.failed > 0) {
        std::println("{}[  FAILED  ] {} tests, listed below:{}", red, summary.failed, clear);
        for(const auto& failed: summary.failed_tests) {
            std::println("{}[  FAILED  ] {}{}", red, failed.name, clear);
            std::println("             at {}:{}", failed.path, failed.line);
        }
        std::println("{}{} FAILED TEST{}{}",
                     red,
                     summary.failed,
                     summary.failed == 1 ? "" : "S",
                     clear);
    }
}

}  // namespace

namespace eventide::zest {

int run_cli(int argc, char** argv, std::string_view command_overview) {
    auto args = deco::util::argvify(argc, argv);
    deco::cli::Dispatcher<ZestCliOptions> dispatcher(command_overview);

    auto parsed = deco::cli::parse<ZestCliOptions>(args);
    if(!parsed.has_value()) {
        std::cerr << "Error parsing options: " << parsed.error().message << "\n";
        dispatcher.usage(std::cerr);
        std::exit(1);
    }

    auto options = to_runner_options(std::move(parsed->options));
    if(!options.has_value()) {
        std::cerr << "Error parsing options: " << options.error() << "\n";
        dispatcher.usage(std::cerr);
        std::exit(1);
    }

    return run_tests(std::move(*options));
}

int run_tests(RunnerOptions options) {
    return Runner::instance().run_tests(std::move(options));
}

int run_tests(std::string_view filter) {
    return Runner::instance().run_tests(filter);
}

Runner& Runner::instance() {
    static Runner runner;
    return runner;
}

void Runner::add_suite(std::string_view name, std::vector<TestCase> (*cases)()) {
    suites.emplace_back(std::string(name), cases);
}

int Runner::run_tests(std::string_view filter) {
    return run_tests(RunnerOptions{.filter = std::string(filter)});
}

int Runner::run_tests(RunnerOptions options) {
    const auto patterns = resolve_filter_patterns(options.filter);
    auto grouped_suites = group_suites(suites);
    const bool focus_mode = has_focused_tests(grouped_suites, patterns);

    RunSummary summary;

    std::println("{}[----------] Global test environment set-up.{}", green, clear);
    if(focus_mode) {
        std::println("{}[  FOCUS   ] Running in focus-only mode.{}", yellow, clear);
    }

    for(auto& [suite_name, test_cases]: grouped_suites) {
        if(!matches_suite_filter(suite_name, patterns)) {
            continue;
        }

        bool suite_has_tests = false;

        for(auto& test_case: test_cases) {
            if(!matches_test_filter(suite_name, test_case.name, patterns)) {
                continue;
            }

            const auto display_name = make_display_name(suite_name, test_case.name);
            suite_has_tests = true;

            if(focus_mode && !test_case.attrs.focus) {
                summary.skipped += 1;
                continue;
            }

            if(test_case.attrs.skip) {
                if(!options.only_failed_output) {
                    std::println("{}[ SKIPPED  ] {}{}", yellow, display_name, clear);
                }
                summary.skipped += 1;
                continue;
            }

            if(!options.only_failed_output) {
                std::println("{}[ RUN      ] {}{}", green, display_name, clear);
            }
            summary.tests += 1;

            using namespace std::chrono;
            auto begin = system_clock::now();
            auto state = test_case.test();
            auto end = system_clock::now();

            auto duration = duration_cast<milliseconds>(end - begin);
            const bool failed = is_failure(state);
            print_run_result(display_name, failed, duration, options.only_failed_output);

            summary.duration += duration;
            if(failed) {
                summary.failed += 1;
                summary.failed_tests.push_back(
                    FailedTest{display_name, test_case.path, test_case.line});
            }
        }

        if(suite_has_tests) {
            summary.suites += 1;
        }
    }

    print_summary(summary);
    return summary.failed != 0;
}

}  // namespace eventide::zest
