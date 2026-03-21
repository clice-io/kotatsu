#pragma once

#include "eventide/zest/detail/registry.h"
#include "eventide/common/fixed_string.h"

namespace eventide::zest {

template <fixed_string TestName, typename Derived>
struct TestSuiteDef {
private:
    TestState state = TestState::Passed;

public:
    using Self = Derived;

    void failure() {
        state = TestState::Failed;
    }

    void pass() {
        state = TestState::Passed;
    }

    void skip() {
        state = TestState::Skipped;
    }

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
        auto run_test = +[] -> TestState {
            Derived test;
            if constexpr(requires { test.setup(); }) {
                test.setup();
            }

            (test.*test_body)();

            if constexpr(requires { test.teardown(); }) {
                test.teardown();
            }

            return test.state;
        };

        test_cases().emplace_back(case_name.data(), path.data(), line, attrs, run_test);
        return true;
    }();
};

}  // namespace eventide::zest
