#pragma once

#include "kota/zest/detail/check.h"
#include "kota/zest/detail/snapshot.h"
#include "kota/zest/detail/suite.h"
#include "kota/zest/detail/trace.h"

#define TEST_SUITE(name, ...)                                                                      \
    struct name##TEST : __VA_OPT__(__VA_ARGS__, )::kota::zest::TestSuiteDef<#name, name##TEST>

// clang-format off
#define ZEST_MAKE_ATTRS(...)                                                                       \
    [] constexpr {                                                                                 \
        ::kota::zest::TestAttrs _a{};                                                          \
        [[maybe_unused]] auto& [skip, focus, serial] = _a;                                         \
        __VA_ARGS__;                                                                               \
        return _a;                                                                                 \
    }()
// clang-format on

#define TEST_SUITE_ATTRS(...)                                                                      \
    constexpr static ::kota::zest::TestAttrs suite_attrs = ZEST_MAKE_ATTRS(__VA_ARGS__)

#define TEST_CASE(name, ...)                                                                       \
    void _register_##name() {                                                                      \
        constexpr auto file_name = std::source_location::current().file_name();                    \
        constexpr auto file_len = std::string_view(file_name).size();                              \
        (void)_register_suites<>;                                                                  \
        constexpr auto _zest_attrs_ = ZEST_MAKE_ATTRS(__VA_OPT__(__VA_ARGS__));                    \
        (void)_register_test_case<#name,                                                           \
                                  &Self::test_##name,                                              \
                                  ::kota::fixed_string<file_len>(file_name),                       \
                                  std::source_location::current().line(),                          \
                                  _zest_attrs_>;                                                   \
    }                                                                                              \
    void test_##name()

#define ZEST_CHECK_IMPL(condition, return_action)                                                  \
    do {                                                                                           \
        if(condition) [[unlikely]] {                                                               \
            ::kota::zest::print_trace(std::source_location::current());                            \
            ::kota::zest::failure();                                                               \
            return_action;                                                                         \
        }                                                                                          \
    } while(0)

#define ZEST_EXPECT_UNARY(expectation, failure_pred, return_action, ...)                           \
    do {                                                                                           \
        auto failed = ([&](auto&& value) {                                                         \
            return ::kota::zest::check_unary_failure((failure_pred),                               \
                                                     #__VA_ARGS__,                                 \
                                                     (expectation),                                \
                                                     value);                                       \
        }(__VA_ARGS__));                                                                           \
        ZEST_CHECK_IMPL(failed, return_action);                                                    \
    } while(0)

#define ZEST_EXPECT_BINARY(op_string, failure_pred, return_action, ...)                            \
    do {                                                                                           \
        auto failed = ([&](auto&& lhs, auto&& rhs) {                                               \
            const auto exprs = ::kota::zest::parse_binary_exprs(#__VA_ARGS__);                     \
            return ::kota::zest::check_binary_failure((failure_pred),                              \
                                                      #op_string,                                  \
                                                      exprs.lhs,                                   \
                                                      exprs.rhs,                                   \
                                                      lhs,                                         \
                                                      rhs);                                        \
        }(__VA_ARGS__));                                                                           \
        ZEST_CHECK_IMPL(failed, return_action);                                                    \
    } while(0)

// STATIC variants wrap the failure predicate in std::bool_constant<> to force
// constant evaluation. Macro args are spliced directly into the predicate, so
// the predicate text uses `__VA_ARGS__` rather than the `lhs`/`rhs` identifiers
// the runtime lambdas bind — STATIC has no double-eval concern to work around.
#define ZEST_STATIC_EXPECT_UNARY(expectation, failure_pred, return_action, ...)                    \
    ZEST_CHECK_IMPL(::kota::zest::check_unary_failure(std::bool_constant<(failure_pred)>(),        \
                                                      #__VA_ARGS__,                                \
                                                      (expectation),                               \
                                                      (__VA_ARGS__)),                              \
                    return_action)

#define ZEST_STATIC_EXPECT_BINARY(op_string, failure_pred, return_action, ...)                     \
    do {                                                                                           \
        const auto exprs = ::kota::zest::parse_binary_exprs(#__VA_ARGS__);                         \
        ZEST_CHECK_IMPL(::kota::zest::check_binary_failure(std::bool_constant<(failure_pred)>(),   \
                                                           #op_string,                             \
                                                           exprs.lhs,                              \
                                                           exprs.rhs,                              \
                                                           __VA_ARGS__),                           \
                        return_action);                                                            \
    } while(false)

// clang-format off
#define EXPECT_TRUE(...) ZEST_EXPECT_UNARY("true", !(value), (void)0, __VA_ARGS__)
#define EXPECT_FALSE(...) ZEST_EXPECT_UNARY("false", (value), (void)0, __VA_ARGS__)
#define EXPECT_EQ(...) ZEST_EXPECT_BINARY(==, !::kota::meta::eq(lhs, rhs), (void)0, __VA_ARGS__)
#define EXPECT_NE(...) ZEST_EXPECT_BINARY(!=, ::kota::meta::eq(lhs, rhs), (void)0, __VA_ARGS__)
#define EXPECT_LT(...) ZEST_EXPECT_BINARY(<, !::kota::meta::lt(lhs, rhs), (void)0, __VA_ARGS__)
#define EXPECT_LE(...) ZEST_EXPECT_BINARY(<=, !::kota::meta::le(lhs, rhs), (void)0, __VA_ARGS__)
#define EXPECT_GT(...) ZEST_EXPECT_BINARY(>, !::kota::meta::gt(lhs, rhs), (void)0, __VA_ARGS__)
#define EXPECT_GE(...) ZEST_EXPECT_BINARY(>=, !::kota::meta::ge(lhs, rhs), (void)0, __VA_ARGS__)

// EXPECT_TYPE_EQ(L, R) — forwards two type args to std::is_same_v; on mismatch,
// prints both sides' type names via kota::meta::type_name for diagnostics.
#define EXPECT_TYPE_EQ(...)                                                                        \
    ZEST_CHECK_IMPL((::kota::zest::check_type_eq_failure<__VA_ARGS__>(#__VA_ARGS__)), (void)0)

// clang-format off
#define STATIC_EXPECT_TRUE(...) ZEST_STATIC_EXPECT_UNARY("true", !(__VA_ARGS__), (void)0, __VA_ARGS__)
#define STATIC_EXPECT_FALSE(...) ZEST_STATIC_EXPECT_UNARY("false", (__VA_ARGS__), (void)0, __VA_ARGS__)
#define STATIC_EXPECT_EQ(...) ZEST_STATIC_EXPECT_BINARY(==, !::kota::meta::eq(__VA_ARGS__), (void)0, __VA_ARGS__)
#define STATIC_EXPECT_NE(...) ZEST_STATIC_EXPECT_BINARY(!=, ::kota::meta::eq(__VA_ARGS__), (void)0, __VA_ARGS__)
#define STATIC_EXPECT_LT(...) ZEST_STATIC_EXPECT_BINARY(<, !::kota::meta::lt(__VA_ARGS__), (void)0, __VA_ARGS__)
#define STATIC_EXPECT_LE(...) ZEST_STATIC_EXPECT_BINARY(<=, !::kota::meta::le(__VA_ARGS__), (void)0, __VA_ARGS__)
#define STATIC_EXPECT_GT(...) ZEST_STATIC_EXPECT_BINARY(>, !::kota::meta::gt(__VA_ARGS__), (void)0, __VA_ARGS__)
#define STATIC_EXPECT_GE(...) ZEST_STATIC_EXPECT_BINARY(>=, !::kota::meta::ge(__VA_ARGS__), (void)0, __VA_ARGS__)
// clang-format on

#define ASSERT_TRUE(...) ZEST_EXPECT_UNARY("true", !(value), return, __VA_ARGS__)
#define ASSERT_FALSE(...) ZEST_EXPECT_UNARY("false", (value), return, __VA_ARGS__)
#define ASSERT_EQ(...) ZEST_EXPECT_BINARY(==, !::kota::meta::eq(lhs, rhs), return, __VA_ARGS__)
#define ASSERT_NE(...) ZEST_EXPECT_BINARY(!=, ::kota::meta::eq(lhs, rhs), return, __VA_ARGS__)
#define ASSERT_LT(...) ZEST_EXPECT_BINARY(<, !::kota::meta::lt(lhs, rhs), return, __VA_ARGS__)
#define ASSERT_LE(...) ZEST_EXPECT_BINARY(<=, !::kota::meta::le(lhs, rhs), return, __VA_ARGS__)
#define ASSERT_GT(...) ZEST_EXPECT_BINARY(>, !::kota::meta::gt(lhs, rhs), return, __VA_ARGS__)
#define ASSERT_GE(...) ZEST_EXPECT_BINARY(>=, !::kota::meta::ge(lhs, rhs), return, __VA_ARGS__)

#define CO_ASSERT_TRUE(...) ZEST_EXPECT_UNARY("true", !(value), co_return, __VA_ARGS__)
#define CO_ASSERT_FALSE(...) ZEST_EXPECT_UNARY("false", (value), co_return, __VA_ARGS__)
#define CO_ASSERT_EQ(...)                                                                          \
    ZEST_EXPECT_BINARY(==, !::kota::meta::eq(lhs, rhs), co_return, __VA_ARGS__)
#define CO_ASSERT_NE(...) ZEST_EXPECT_BINARY(!=, ::kota::meta::eq(lhs, rhs), co_return, __VA_ARGS__)
#define CO_ASSERT_LT(...) ZEST_EXPECT_BINARY(<, !::kota::meta::lt(lhs, rhs), co_return, __VA_ARGS__)
#define CO_ASSERT_LE(...)                                                                          \
    ZEST_EXPECT_BINARY(<=, !::kota::meta::le(lhs, rhs), co_return, __VA_ARGS__)
#define CO_ASSERT_GT(...) ZEST_EXPECT_BINARY(>, !::kota::meta::gt(lhs, rhs), co_return, __VA_ARGS__)
#define CO_ASSERT_GE(...)                                                                          \
    ZEST_EXPECT_BINARY(>=, !::kota::meta::ge(lhs, rhs), co_return, __VA_ARGS__)
// clang-format on

// clang-format off
#define ZEST_SNAPSHOT_STR_IMPL(return_action, value, ...)                                          \
    ZEST_CHECK_IMPL(::kota::zest::check_snapshot(value __VA_OPT__(, __VA_ARGS__)), return_action)

#define EXPECT_SNAPSHOT(value, ...) ZEST_SNAPSHOT_STR_IMPL((void)0, value __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_SNAPSHOT(value, ...) ZEST_SNAPSHOT_STR_IMPL(return, value __VA_OPT__(,) __VA_ARGS__)
#define CO_ASSERT_SNAPSHOT(value, ...) ZEST_SNAPSHOT_STR_IMPL(co_return, value __VA_OPT__(,) __VA_ARGS__)

#define ZEST_SNAPSHOT_GLOB_IMPL(return_action, pattern, transform)                                 \
    ZEST_CHECK_IMPL(::kota::zest::check_snapshot_glob(pattern, transform), return_action)

#define EXPECT_SNAPSHOT_GLOB(pattern, transform) ZEST_SNAPSHOT_GLOB_IMPL((void)0, pattern, transform)
#define ASSERT_SNAPSHOT_GLOB(pattern, transform) ZEST_SNAPSHOT_GLOB_IMPL(return, pattern, transform)
#define CO_ASSERT_SNAPSHOT_GLOB(pattern, transform) ZEST_SNAPSHOT_GLOB_IMPL(co_return, pattern, transform)
// clang-format on

#ifdef __cpp_exceptions

#define CAUGHT(print, ...) (::kota::zest::trace_exception([&]() { (__VA_ARGS__); }, print))

#define ZEST_EXPECT_THROWS(expectation, failure_pred, return_action, ...)                          \
    do {                                                                                           \
        auto failed = ([&]() {                                                                     \
            return ::kota::zest::check_throws_failure((failure_pred),                              \
                                                      #__VA_ARGS__,                                \
                                                      (expectation));                              \
        }());                                                                                      \
        ZEST_CHECK_IMPL(failed, return_action);                                                    \
    } while(0)

// clang-format off
#define EXPECT_THROWS(...) ZEST_EXPECT_THROWS("throw exception", !CAUGHT(false, __VA_ARGS__), (void)0, __VA_ARGS__)
#define EXPECT_NOTHROWS(...) ZEST_EXPECT_THROWS("not throw exception", CAUGHT(true, __VA_ARGS__), (void)0, __VA_ARGS__)
// clang-format on

#endif
