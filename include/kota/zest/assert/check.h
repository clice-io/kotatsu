#pragma once

#include <cstdint>
#include <format>
#include <functional>
#include <initializer_list>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "kota/zest/assert/trace.h"
#include "kota/meta/compare.h"
#include "kota/meta/name.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/debug/encode.h"

namespace kota::zest {

/// Renders `value` for a failure report, through the debug codec.
template <typename T>
std::string pretty_dump(const T& value) {
    auto text = codec::debug::to_string(value);
    return text ? std::move(*text) : std::string(meta::type_name<T>());
}

/// What a predicate found: whether it held, and how to show its inputs when a
/// check on it fails.
struct Match {
    bool held;
    /// Builds the explanation; called only for a failing check, while the
    /// predicate's arguments are still alive.
    std::function<std::string()> explain;

    /// Negating keeps the explanation: the inputs are what they were.
    Match operator!() const {
        return Match{!held, explain};
    }
};

/// Pushes a line into the report of every check failing while it is alive.
/// Use it through ZEST_CONTEXT.
class Context {
public:
    explicit Context(std::string message);

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    ~Context();

    const std::string& message() const {
        return text;
    }

private:
    std::string text;
};

struct ReportLine {
    std::string_view label;
    std::string text;
};

/// Prints a failed check with its lines and the contexts in scope, and fails
/// the running test.
void report_failure(std::string_view expression,
                    std::initializer_list<ReportLine> lines,
                    std::source_location location);

namespace detail {

template <typename T>
constexpr bool dependent_false = false;

template <typename T>
constexpr bool is_c_string =
    std::is_pointer_v<T> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<T>>, char>;

template <typename T>
constexpr bool is_char_array =
    std::is_array_v<T> && std::is_same_v<std::remove_cv_t<std::remove_extent_t<T>>, char>;

template <typename T>
constexpr bool is_text = is_c_string<T> || is_char_array<T> || meta::str_like<T>;

/// A char pointer compared with text that is not a pointer, such as a string
/// literal, is compared as text; two pointers still compare by address.
template <typename L, typename R>
constexpr bool compares_as_text = (is_c_string<L> && is_text<R> && !std::is_pointer_v<R>) ||
                                  (is_c_string<R> && is_text<L> && !std::is_pointer_v<L>);

/// Text of a string-like value; nothing for a null pointer.
template <typename T>
constexpr std::optional<std::string_view> as_text(const T& value) {
    if constexpr(std::is_pointer_v<T>) {
        if(value == nullptr) {
            return std::nullopt;
        }
    }
    return std::string_view(value);
}

enum class Relation : std::uint8_t {
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

template <Relation R, typename L, typename Rhs>
constexpr bool relate(const L& lhs, const Rhs& rhs) {
    if constexpr(compares_as_text<L, Rhs>) {
        auto l = as_text(lhs);
        auto r = as_text(rhs);
        if constexpr(R == Relation::Equal) {
            return l == r;
        } else if constexpr(R == Relation::NotEqual) {
            return l != r;
        } else if constexpr(R == Relation::Less) {
            return l < r;
        } else if constexpr(R == Relation::LessEqual) {
            return l <= r;
        } else if constexpr(R == Relation::Greater) {
            return l > r;
        } else {
            return l >= r;
        }
    } else if constexpr(R == Relation::Equal) {
        return meta::eq(lhs, rhs);
    } else if constexpr(R == Relation::NotEqual) {
        return meta::ne(lhs, rhs);
    } else if constexpr(R == Relation::Less) {
        return meta::lt(lhs, rhs);
    } else if constexpr(R == Relation::LessEqual) {
        return meta::le(lhs, rhs);
    } else if constexpr(R == Relation::Greater) {
        return meta::gt(lhs, rhs);
    } else {
        return meta::ge(lhs, rhs);
    }
}

/// Renders an operand, spelling out a null char pointer.
template <typename T>
std::string dump_operand(const T& value) {
    if constexpr(is_c_string<T>) {
        if(value == nullptr) {
            return "nullptr";
        }
    }
    return pretty_dump(value);
}

}  // namespace detail

/// A comparison split into its operands. `L` and `R` are forwarding-deduced,
/// so the members are references into the check's own full-expression.
template <typename L, typename R>
struct Comparison {
    L&& lhs;
    R&& rhs;
    bool held;

    // clang-format off
    template <typename U> constexpr void operator==(U&&) && { reject_chain<U>(); }
    template <typename U> constexpr void operator!=(U&&) && { reject_chain<U>(); }
    template <typename U> constexpr void operator<(U&&) && { reject_chain<U>(); }
    template <typename U> constexpr void operator<=(U&&) && { reject_chain<U>(); }
    template <typename U> constexpr void operator>(U&&) && { reject_chain<U>(); }
    template <typename U> constexpr void operator>=(U&&) && { reject_chain<U>(); }
    template <typename U> constexpr void operator&&(U&&) && { reject_logic<U>(); }
    template <typename U> constexpr void operator||(U&&) && { reject_logic<U>(); }
    template <typename U> constexpr void operator&(U&&) && { reject_logic<U>(); }
    template <typename U> constexpr void operator|(U&&) && { reject_logic<U>(); }
    template <typename U> constexpr void operator^(U&&) && { reject_logic<U>(); }

    // clang-format on

private:
    template <typename U>
    constexpr static void reject_chain() {
        static_assert(detail::dependent_false<U>,
                      "a check takes one comparison; split `a < b < c` into two checks");
    }

    template <typename U>
    constexpr static void reject_logic() {
        static_assert(detail::dependent_false<U>,
                      "split `a && b` into two checks, or parenthesize it to check one bool");
    }
};

/// The left operand of a check, or its whole expression when it compares
/// nothing.
template <typename T>
struct Operand {
    T&& value;

    // clang-format off
    template <typename R> constexpr auto operator==(R&& rhs) && { return compare<detail::Relation::Equal>(std::forward<R>(rhs)); }
    template <typename R> constexpr auto operator!=(R&& rhs) && { return compare<detail::Relation::NotEqual>(std::forward<R>(rhs)); }
    template <typename R> constexpr auto operator<(R&& rhs) && { return compare<detail::Relation::Less>(std::forward<R>(rhs)); }
    template <typename R> constexpr auto operator<=(R&& rhs) && { return compare<detail::Relation::LessEqual>(std::forward<R>(rhs)); }
    template <typename R> constexpr auto operator>(R&& rhs) && { return compare<detail::Relation::Greater>(std::forward<R>(rhs)); }
    template <typename R> constexpr auto operator>=(R&& rhs) && { return compare<detail::Relation::GreaterEqual>(std::forward<R>(rhs)); }
    template <typename U> constexpr void operator&&(U&&) && { reject_logic<U>(); }
    template <typename U> constexpr void operator||(U&&) && { reject_logic<U>(); }
    template <typename U> constexpr void operator&(U&&) && { reject_logic<U>(); }
    template <typename U> constexpr void operator|(U&&) && { reject_logic<U>(); }
    template <typename U> constexpr void operator^(U&&) && { reject_logic<U>(); }
    template <typename U> constexpr void operator<<(U&&) && { reject_shift<U>(); }
    template <typename U> constexpr void operator>>(U&&) && { reject_shift<U>(); }

    // clang-format on

private:
    template <typename U>
    constexpr static void reject_shift() {
        static_assert(detail::dependent_false<U>,
                      "a check splits at `<<`; parenthesize a shift, as in `(a << 2) == b`");
    }

    template <detail::Relation Rel, typename R>
    constexpr Comparison<T, R> compare(R&& rhs) {
        bool held = detail::relate<Rel>(std::as_const(value), std::as_const(rhs));
        return Comparison<T, R>{std::forward<T>(value), std::forward<R>(rhs), held};
    }

    template <typename U>
    constexpr static void reject_logic() {
        static_assert(detail::dependent_false<U>,
                      "split `a && b` into two checks, or parenthesize it to check one bool");
    }
};

/// Starts a check: `Decomposer{} << expr` captures the left operand of a
/// top-level comparison in `expr`, or `expr` itself. Not `<=`: C++20 would also
/// try `expr <=> Decomposer`, and std::tuple's `<=>` breaks on that.
struct Decomposer {
    template <typename T>
    friend constexpr Operand<T> operator<<(Decomposer, T&& value) {
        return Operand<T>{std::forward<T>(value)};
    }
};

template <typename T>
constexpr bool holds(Operand<T>&& operand) {
    if constexpr(std::is_same_v<std::remove_cvref_t<T>, Match>) {
        return operand.value.held;
    } else {
        return static_cast<bool>(operand.value);
    }
}

template <typename L, typename R>
constexpr bool holds(Comparison<L, R>&& comparison) {
    return comparison.held;
}

/// Reports `operand` if it does not hold; returns whether it held.
template <typename T>
bool check(Operand<T>&& operand,
           std::string_view expression,
           std::source_location location = std::source_location::current()) {
    using V = std::remove_cvref_t<T>;
    if constexpr(std::is_same_v<V, Match>) {
        if(operand.value.held) {
            return true;
        }
        report_failure(expression,
                       {
                           {"", operand.value.explain()}
        },
                       location);
    } else if constexpr(std::is_same_v<V, bool>) {
        if(operand.value) {
            return true;
        }
        report_failure(expression, {}, location);
    } else {
        if(static_cast<bool>(operand.value)) {
            return true;
        }
        report_failure(expression,
                       {
                           {"got", detail::dump_operand(operand.value)}
        },
                       location);
    }
    return false;
}

/// Reports `comparison` if it does not hold; returns whether it held.
template <typename L, typename R>
bool check(Comparison<L, R>&& comparison,
           std::string_view expression,
           std::source_location location = std::source_location::current()) {
    if(comparison.held) {
        return true;
    }
    report_failure(expression,
                   {
                       {"lhs", detail::dump_operand(comparison.lhs)},
                       {"rhs", detail::dump_operand(comparison.rhs)},
    },
                   location);
    return false;
}

#ifdef __cpp_exceptions

/// Runs `body` and reports whether it threw as `expect_throw` says it should.
void check_throws(function<void()> body,
                  std::string_view expression,
                  bool expect_throw,
                  std::source_location location = std::source_location::current());

#endif

}  // namespace kota::zest
