#pragma once

#include "eventide/zest/detail/check.h"
#include "eventide/zest/detail/suite.h"
#include "eventide/zest/detail/trace.h"

#define TEST_SUITE(name, ...)                                                                      \
    struct name##TEST : __VA_OPT__(__VA_ARGS__, )::eventide::zest::TestSuiteDef<#name, name##TEST>

#define TEST_CASE(name, ...)                                                                       \
    void _register_##name() {                                                                      \
        constexpr auto file_name = std::source_location::current().file_name();                    \
        constexpr auto file_len = std::string_view(file_name).size();                              \
        (void)_register_suites<>;                                                                  \
        (void)_register_test_case<#name,                                                           \
                                  &Self::test_##name,                                              \
                                  ::eventide::fixed_string<file_len>(file_name),                   \
                                  std::source_location::current().line() __VA_OPT__(, )            \
                                      __VA_ARGS__>;                                                \
    }                                                                                              \
    void test_##name()

#define ZEST_CHECK_IMPL(condition, return_action)                                                  \
    do {                                                                                           \
        if(condition) [[unlikely]] {                                                               \
            ::eventide::zest::print_trace(std::source_location::current());                        \
            ::eventide::zest::failure();                                                           \
            return_action;                                                                         \
        }                                                                                          \
    } while(0)

#define ZEST_EXPECT_UNARY(expectation, failure_pred, return_action, ...)                           \
    do {                                                                                           \
        auto failed = ([&](auto&& value) {                                                         \
            return ::eventide::zest::check_unary_failure((failure_pred),                           \
                                                         #__VA_ARGS__,                             \
                                                         (expectation),                            \
                                                         value);                                   \
        }(__VA_ARGS__));                                                                           \
        ZEST_CHECK_IMPL(failed, return_action);                                                    \
    } while(0)

#define ZEST_EXPECT_BINARY(op_string, failure_pred, return_action, ...)                            \
    do {                                                                                           \
        auto failed = ([&](auto&& lhs, auto&& rhs) {                                               \
            const auto exprs = ::eventide::zest::parse_binary_exprs(#__VA_ARGS__);                 \
            return ::eventide::zest::check_binary_failure((failure_pred),                          \
                                                          #op_string,                              \
                                                          exprs.lhs,                               \
                                                          exprs.rhs,                               \
                                                          lhs,                                     \
                                                          rhs);                                    \
        }(__VA_ARGS__));                                                                           \
        ZEST_CHECK_IMPL(failed, return_action);                                                    \
    } while(0)

// clang-format off
#define EXPECT_TRUE(...) ZEST_EXPECT_UNARY("true", !(value), (void)0, __VA_ARGS__)
#define EXPECT_FALSE(...) ZEST_EXPECT_UNARY("false", (value), (void)0, __VA_ARGS__)
#define EXPECT_EQ(...) ZEST_EXPECT_BINARY(==, !::eventide::refl::eq(lhs, rhs), (void)0, __VA_ARGS__)
#define EXPECT_NE(...) ZEST_EXPECT_BINARY(!=, ::eventide::refl::eq(lhs, rhs), (void)0, __VA_ARGS__)
#define EXPECT_LT(...) ZEST_EXPECT_BINARY(<, !::eventide::refl::lt(lhs, rhs), (void)0, __VA_ARGS__)
#define EXPECT_LE(...) ZEST_EXPECT_BINARY(<=, !::eventide::refl::le(lhs, rhs), (void)0, __VA_ARGS__)
#define EXPECT_GT(...) ZEST_EXPECT_BINARY(>, !::eventide::refl::gt(lhs, rhs), (void)0, __VA_ARGS__)
#define EXPECT_GE(...) ZEST_EXPECT_BINARY(>=, !::eventide::refl::ge(lhs, rhs), (void)0, __VA_ARGS__)

#define ASSERT_TRUE(...) ZEST_EXPECT_UNARY("true", !(value), return, __VA_ARGS__)
#define ASSERT_FALSE(...) ZEST_EXPECT_UNARY("false", (value), return, __VA_ARGS__)
#define ASSERT_EQ(...) ZEST_EXPECT_BINARY(==, !::eventide::refl::eq(lhs, rhs), return, __VA_ARGS__)
#define ASSERT_NE(...) ZEST_EXPECT_BINARY(!=, ::eventide::refl::eq(lhs, rhs), return, __VA_ARGS__)
#define ASSERT_LT(...) ZEST_EXPECT_BINARY(<, !::eventide::refl::lt(lhs, rhs), return, __VA_ARGS__)
#define ASSERT_LE(...) ZEST_EXPECT_BINARY(<=, !::eventide::refl::le(lhs, rhs), return, __VA_ARGS__)
#define ASSERT_GT(...) ZEST_EXPECT_BINARY(>, !::eventide::refl::gt(lhs, rhs), return, __VA_ARGS__)
#define ASSERT_GE(...) ZEST_EXPECT_BINARY(>=, !::eventide::refl::ge(lhs, rhs), return, __VA_ARGS__)

#define CO_ASSERT_TRUE(...) ZEST_EXPECT_UNARY("true", !(value), co_return, __VA_ARGS__)
#define CO_ASSERT_FALSE(...) ZEST_EXPECT_UNARY("false", (value), co_return, __VA_ARGS__)
#define CO_ASSERT_EQ(...) ZEST_EXPECT_BINARY(==, !::eventide::refl::eq(lhs, rhs), co_return, __VA_ARGS__)
#define CO_ASSERT_NE(...) ZEST_EXPECT_BINARY(!=, ::eventide::refl::eq(lhs, rhs), co_return, __VA_ARGS__)
#define CO_ASSERT_LT(...) ZEST_EXPECT_BINARY(<, !::eventide::refl::lt(lhs, rhs), co_return, __VA_ARGS__)
#define CO_ASSERT_LE(...) ZEST_EXPECT_BINARY(<=, !::eventide::refl::le(lhs, rhs), co_return, __VA_ARGS__)
#define CO_ASSERT_GT(...) ZEST_EXPECT_BINARY(>, !::eventide::refl::gt(lhs, rhs), co_return, __VA_ARGS__)
#define CO_ASSERT_GE(...) ZEST_EXPECT_BINARY(>=, !::eventide::refl::ge(lhs, rhs), co_return, __VA_ARGS__)
// clang-format on

#ifdef __cpp_exceptions

#define CAUGHT(...)                                                                                \
    ([&]() {                                                                                       \
        try {                                                                                      \
            (__VA_ARGS__);                                                                         \
            return false;                                                                          \
        } catch(...) {                                                                             \
            return true;                                                                           \
        }                                                                                          \
    }())

#define ZEST_EXPECT_THROWS(expectation, failure_pred, return_action, ...)                          \
    do {                                                                                           \
        auto failed = ([&]() {                                                                     \
            return ::eventide::zest::check_throws_failure((failure_pred),                          \
                                                          #__VA_ARGS__,                            \
                                                          (expectation));                          \
        }());                                                                                      \
        ZEST_CHECK_IMPL(failed, return_action);                                                    \
    } while(0)

// clang-format off
#define EXPECT_THROWS(...) ZEST_EXPECT_THROWS("throw exception", !CAUGHT(__VA_ARGS__), (void)0, __VA_ARGS__)
#define EXPECT_NOTHROWS(...) ZEST_EXPECT_THROWS("not throw exception", CAUGHT(__VA_ARGS__), (void)0, __VA_ARGS__)
// clang-format on

#endif
