#pragma once

#include <concepts>
#include <expected>
#include <format>
#include <optional>
#include <print>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "eventide/common/meta.h"
#include "eventide/reflection/compare.h"

namespace eventide::zest {

template <typename T>
inline std::string pretty_dump(const T& value) {
    if constexpr(is_optional_v<T>) {
        if(!value) {
            return std::string("nullopt");
        }
        return zest::pretty_dump(*value);
    } else if constexpr(is_expected_v<T>) {
        if(value.has_value()) {
            return std::format("expected({})", zest::pretty_dump(*value));
        }
        return std::format("unexpected({})", zest::pretty_dump(value.error()));
    } else {
        if constexpr(Formattable<T>) {
            return std::format("{}", value);
        } else {
            return std::string("<unformattable>");
        }
    }
}

struct binary_expr_pair {
    std::string_view lhs;
    std::string_view rhs;
};

binary_expr_pair parse_binary_exprs(std::string_view exprs);

template <typename V>
inline bool check_unary_failure(bool failure,
                                std::string_view expr,
                                std::string_view expectation,
                                const V& value,
                                std::source_location loc = std::source_location::current()) {
    if(failure) {
        std::println("[ expect ] {} (expected {})", expr, expectation);
        std::println("           got: {}", zest::pretty_dump(value));
        std::println("           at {}:{}", loc.file_name(), loc.line());
    }
    return failure;
}

template <typename L, typename R>
inline bool check_binary_failure(bool failure,
                                 std::string_view op,
                                 std::string_view lhs_expr,
                                 std::string_view rhs_expr,
                                 const L& lhs,
                                 const R& rhs,
                                 std::source_location loc = std::source_location::current()) {
    if(failure) {
        std::println("[ expect ] {} {} {}", lhs_expr, op, rhs_expr);
        std::println("           lhs: {}", zest::pretty_dump(lhs));
        std::println("           rhs: {}", zest::pretty_dump(rhs));
        std::println("           at {}:{}", loc.file_name(), loc.line());
    }
    return failure;
}

#ifdef __cpp_exceptions

inline bool check_throws_failure(bool failure,
                                 std::string_view expr,
                                 std::string_view expectation,
                                 std::source_location loc = std::source_location::current()) {
    if(failure) {
        std::println("[ expect ] {} (expected {})", expr, expectation);
        std::println("           at {}:{}", loc.file_name(), loc.line());
    }
    return failure;
}

#endif

}  // namespace eventide::zest
