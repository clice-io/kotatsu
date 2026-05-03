#pragma once

#include "kota/zest/detail/registry.h"
#include "kota/zest/detail/snapshot.h"
#include "kota/support/fixed_string.h"

namespace kota::zest {

/// Merge suite-level and case-level test attributes.
/// Case-level flags override suite defaults when explicitly set to true.
constexpr TestAttrs merge_attrs(TestAttrs suite, TestAttrs test_case) {
    return {
        .skip = suite.skip || test_case.skip,
        .focus = suite.focus || test_case.focus,
        .serial = suite.serial || test_case.serial,
    };
}

template <fixed_string TestName, typename Derived>
struct TestSuiteDef {
    using Self = Derived;

    constexpr inline static auto& test_cases() {
        static std::vector<TestCase> instance;
        return instance;
    }

    constexpr inline static auto suites() {
        return std::move(test_cases());
    }

    template <typename T = void>
    inline static bool _register_suites = [] {
        Runner::instance().add_suite(TestName.data(), &suites);
        return true;
    }();

    template <fixed_string case_name,
              auto test_body,
              fixed_string path,
              std::size_t line,
              TestAttrs attrs = {}>
    inline static bool _register_test_case = [] {
        constexpr auto effective_attrs = [] {
            if constexpr(requires { Derived::suite_attrs; }) {
                return merge_attrs(Derived::suite_attrs, attrs);
            } else {
                return attrs;
            }
        }();

        auto run_test = +[] -> TestState {
            current_test_state() = TestState::Passed;
            reset_snapshot_context(TestName.data(), case_name.data(), path.data());
            Derived test;
            if constexpr(requires { test.setup(); }) {
                test.setup();
            }

            (test.*test_body)();

            if constexpr(requires { test.teardown(); }) {
                test.teardown();
            }

            return current_test_state();
        };

        test_cases().emplace_back(case_name.data(), path.data(), line, effective_attrs, run_test);
        return true;
    }();
};

}  // namespace kota::zest
