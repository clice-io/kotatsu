#include "eventide/zest/runner.h"

#include <chrono>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

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

}  // namespace

namespace eventide::zest {

Runner& Runner::instance() {
    static Runner runner;
    return runner;
}

void Runner::add_suite(std::string_view name, std::vector<TestCase> (*cases)()) {
    suites.emplace_back(std::string(name), cases);
}

int Runner::run_tests(std::string_view filter) {
    constexpr std::string_view wildcard = "*";
    std::string suite_pattern{wildcard};
    std::string display_pattern{wildcard};

    if(!filter.empty()) {
        auto dot = filter.find('.');
        if(dot == std::string_view::npos) {
            suite_pattern.assign(filter);
            display_pattern = suite_pattern + ".*";
        } else {
            suite_pattern.assign(filter.substr(0, dot));
            auto test_pattern = filter.substr(dot + 1);
            if(test_pattern.empty()) {
                test_pattern = wildcard;
            }
            display_pattern.assign(filter.substr(0, dot));
            display_pattern.push_back('.');
            display_pattern.append(test_pattern);
        }
    }

    constexpr static std::string_view GREEN = "\033[32m";
    constexpr static std::string_view YELLOW = "\033[33m";
    constexpr static std::string_view RED = "\033[31m";
    constexpr static std::string_view CLEAR = "\033[0m";

    std::uint32_t total_tests_count = 0;
    std::uint32_t total_suites_count = 0;
    std::uint32_t total_failed_tests_count = 0;
    std::uint32_t total_skipped_tests_count = 0;
    std::chrono::milliseconds total_test_duration{0};

    struct FailedTest {
        std::string name;
        std::string path;
        std::size_t line;
    };

    std::vector<FailedTest> failed_tests;

    std::unordered_map<std::string, std::vector<TestCase>> all_suites;

    for(auto suite: suites) {
        auto cases = suite.cases();
        auto& target = all_suites[suite.name];
        for(auto& case_: cases) {
            target.emplace_back(std::move(case_));
        }
    }

    auto matches_suite_filter = [&](std::string_view suite_name) -> bool {
        return matches_pattern(suite_name, suite_pattern);
    };

    auto matches_test_filter = [&](std::string_view suite_name,
                                   std::string_view test_name) -> bool {
        std::string display_name = std::format("{}.{}", suite_name, test_name);
        if(display_pattern == wildcard) {
            return true;
        }
        return matches_pattern(display_name, display_pattern);
    };

    bool focus_mode = false;
    for(auto& [suite_name, test_cases]: all_suites) {
        if(!matches_suite_filter(suite_name)) {
            continue;
        }

        for(auto& test_case: test_cases) {
            if(!matches_test_filter(suite_name, test_case.name)) {
                continue;
            }

            if(test_case.attrs.focus && !test_case.attrs.skip) {
                focus_mode = true;
                break;
            }
        }

        if(focus_mode) {
            break;
        }
    }

    std::println("{}[----------] Global test environment set-up.{}", GREEN, CLEAR);
    if(focus_mode) {
        std::println("{}[  FOCUS   ] Running in focus-only mode.{}", YELLOW, CLEAR);
    }

    for(auto& [suite_name, test_cases]: all_suites) {
        if(!matches_suite_filter(suite_name)) {
            continue;
        }

        bool suite_has_tests = false;

        for(auto& test_case: test_cases) {
            std::string display_name = std::format("{}.{}", suite_name, test_case.name);
            if(!matches_test_filter(suite_name, test_case.name)) {
                continue;
            }

            suite_has_tests = true;

            if(focus_mode && !test_case.attrs.focus) {
                total_skipped_tests_count += 1;
                continue;
            }

            if(test_case.attrs.skip) {
                std::println("{}[ SKIPPED  ] {}{}", YELLOW, display_name, CLEAR);
                total_skipped_tests_count += 1;
                continue;
            }

            std::println("{}[ RUN      ] {}{}", GREEN, display_name, CLEAR);
            total_tests_count += 1;

            using namespace std::chrono;
            auto begin = system_clock::now();
            auto state = test_case.test();
            auto end = system_clock::now();

            bool curr_failed = state == TestState::Failed || state == TestState::Fatal;
            auto duration = duration_cast<milliseconds>(end - begin);
            std::println("{0}[   {1} ] {2} ({3} ms){4}",
                         curr_failed ? RED : GREEN,
                         curr_failed ? "FAILED" : "    OK",
                         display_name,
                         duration.count(),
                         CLEAR);

            total_test_duration += duration;
            if(curr_failed) {
                total_failed_tests_count += 1;
                failed_tests.push_back(FailedTest{display_name, test_case.path, test_case.line});
            }
        }

        if(suite_has_tests) {
            total_suites_count += 1;
        }
    }

    std::println("{}[----------] Global test environment tear-down. {}", GREEN, CLEAR);
    std::println("{}[==========] {} tests from {} test suites ran. ({} ms total){}",
                 GREEN,
                 total_tests_count,
                 total_suites_count,
                 total_test_duration.count(),
                 CLEAR);
    auto total_passed_tests = total_tests_count - total_failed_tests_count;
    if(total_passed_tests > 0) {
        std::println("{}[  PASSED  ] {} tests.{}", GREEN, total_passed_tests, CLEAR);
    }
    if(total_skipped_tests_count > 0) {
        std::println("{}[  SKIPPED ] {} tests.{}", YELLOW, total_skipped_tests_count, CLEAR);
    }
    if(total_failed_tests_count > 0) {
        std::println("{}[  FAILED  ] {} tests, listed below:{}",
                     RED,
                     total_failed_tests_count,
                     CLEAR);
        for(auto& failed: failed_tests) {
            std::println("{}[  FAILED  ] {}{}", RED, failed.name, CLEAR);
            std::println("             at {}:{}", failed.path, failed.line);
        }
        std::println("{}{} FAILED TEST{}{}",
                     RED,
                     total_failed_tests_count,
                     total_failed_tests_count == 1 ? "" : "S",
                     CLEAR);
    }
    return total_failed_tests_count != 0;
}

}  // namespace eventide::zest
