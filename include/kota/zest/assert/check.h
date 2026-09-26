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
#include <vector>

#include "kota/support/functional.h"
#include "kota/support/type_traits.h"
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
        return Match{.held = !held, .explain = explain};
    }
};

/// Adds a line to the report of every check that fails while it is alive.
/// Use it through ZEST_CONTEXT.
struct Context {
    std::string message;

    template <typename... Args>
    explicit Context(std::format_string<Args...> format, Args&&... args) :
        message(std::format(format, std::forward<Args>(args)...)) {
        enter();
    }

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    ~Context();

private:
    void enter();

    /// The stack of the thread that entered this context, which it leaves.
    std::vector<const Context*>* stack = nullptr;
};

namespace detail {

struct ReportLine {
    std::string_view label;
    std::string text;
};

/// Prints a failed check with its lines and the contexts in scope, and fails
/// the running test.
void report_failure(std::string_view expression,
                    std::initializer_list<ReportLine> lines,
                    std::source_location location);

template <typename U>
constexpr void reject_logic() {
    static_assert(dependent_false<U>,
                  "split `a && b` into two checks, or parenthesize it to check one bool");
}

template <typename U>
constexpr void reject_chain() {
    static_assert(dependent_false<U>,
                  "a check takes one comparison; split `a < b < c` into two checks");
}

template <typename U>
constexpr void reject_bitwise() {
    static_assert(dependent_false<U>,
                  "parenthesize a bitwise expression, as in `(flags & mask) != 0`");
}

template <typename U>
constexpr void reject_shift() {
    static_assert(dependent_false<U>,
                  "a check splits at `<<`; parenthesize a shift, as in `(a << 2) == b`");
}

template <typename T>
constexpr bool is_c_string =
    std::is_pointer_v<T> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<T>>, char>;

/// A char pointer compared with text that is not a pointer, such as a string
/// literal, is compared as text; two pointers still compare by address.
template <typename L, typename R>
constexpr bool compares_as_text = (is_c_string<L> && meta::str_like<R> && !std::is_pointer_v<R>) ||
                                  (is_c_string<R> && meta::str_like<L> && !std::is_pointer_v<L>);

/// Text of a string-like value; nothing for a null pointer. A char array need
/// not end in a null character, so its text stops at the array's end.
template <typename T>
constexpr std::optional<std::string_view> as_text(const T& value) {
    if constexpr(std::is_pointer_v<T>) {
        if(value == nullptr) {
            return std::nullopt;
        }
    }
    if constexpr(std::is_array_v<T>) {
        std::string_view text(value, std::extent_v<T>);
        return text.substr(0, text.find('\0'));
    } else {
        return std::string_view(value);
    }
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
        return relate<R>(as_text(lhs), as_text(rhs));
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

/// A comparison split into its operands. `L` and `R` are forwarding-deduced,
/// so the members are references into the check's own full-expression.
template <typename L, typename R>
struct Comparison {
    L&& lhs;
    R&& rhs;
    bool held;

    constexpr bool holds() const {
        return held;
    }

    void fail(std::string_view expression, std::source_location location) const {
        report_failure(expression,
                       {
                           {.label = "lhs", .text = pretty_dump(lhs)},
                           {.label = "rhs", .text = pretty_dump(rhs)},
        },
                       location);
    }

    // clang-format off
    template <typename U> constexpr void operator==(U&&) && { reject_chain<U>(); }
    template <typename U> constexpr void operator!=(U&&) && { reject_chain<U>(); }
    template <typename U> constexpr void operator<(U&&) && { reject_chain<U>(); }
    template <typename U> constexpr void operator<=(U&&) && { reject_chain<U>(); }
    template <typename U> constexpr void operator>(U&&) && { reject_chain<U>(); }
    template <typename U> constexpr void operator>=(U&&) && { reject_chain<U>(); }
    template <typename U> constexpr void operator&&(U&&) && { reject_logic<U>(); }
    template <typename U> constexpr void operator||(U&&) && { reject_logic<U>(); }
    template <typename U> constexpr void operator&(U&&) && { reject_bitwise<U>(); }
    template <typename U> constexpr void operator|(U&&) && { reject_bitwise<U>(); }
    template <typename U> constexpr void operator^(U&&) && { reject_bitwise<U>(); }

    // clang-format on
};

/// Nests Operand so that argument-dependent lookup of an operator on it does
/// not search the classes of `T`, as it would for a specialization Operand<T>.
/// For a std::expected operand, that search finds the expected's `operator==`
/// for any right-hand type, which some standard libraries then check against
/// the operand itself, recursively.
template <typename T>
struct Captured {
    /// The left operand of a check, or its whole expression when it compares
    /// nothing.
    struct Operand {
        T&& value;

        constexpr bool holds() const {
            if constexpr(std::is_same_v<std::remove_cvref_t<T>, Match>) {
                return value.held;
            } else {
                return static_cast<bool>(value);
            }
        }

        void fail(std::string_view expression, std::source_location location) const {
            using V = std::remove_cvref_t<T>;
            if constexpr(std::is_same_v<V, Match>) {
                report_failure(expression,
                               {
                                   {.label = "", .text = value.explain()}
                },
                               location);
            } else if constexpr(std::is_same_v<V, bool>) {
                report_failure(expression, {}, location);
            } else {
                report_failure(expression,
                               {
                                   {.label = "got", .text = pretty_dump(value)}
                },
                               location);
            }
        }

        // clang-format off
        template <typename R> constexpr auto operator==(R&& rhs) && { return compare<Relation::Equal>(std::forward<R>(rhs)); }
        template <typename R> constexpr auto operator!=(R&& rhs) && { return compare<Relation::NotEqual>(std::forward<R>(rhs)); }
        template <typename R> constexpr auto operator<(R&& rhs) && { return compare<Relation::Less>(std::forward<R>(rhs)); }
        template <typename R> constexpr auto operator<=(R&& rhs) && { return compare<Relation::LessEqual>(std::forward<R>(rhs)); }
        template <typename R> constexpr auto operator>(R&& rhs) && { return compare<Relation::Greater>(std::forward<R>(rhs)); }
        template <typename R> constexpr auto operator>=(R&& rhs) && { return compare<Relation::GreaterEqual>(std::forward<R>(rhs)); }
        template <typename U> constexpr void operator&&(U&&) && { reject_logic<U>(); }
        template <typename U> constexpr void operator||(U&&) && { reject_logic<U>(); }
        template <typename U> constexpr void operator&(U&&) && { reject_bitwise<U>(); }
        template <typename U> constexpr void operator|(U&&) && { reject_bitwise<U>(); }
        template <typename U> constexpr void operator^(U&&) && { reject_bitwise<U>(); }
        template <typename U> constexpr void operator<<(U&&) && { reject_shift<U>(); }
        template <typename U> constexpr void operator>>(U&&) && { reject_shift<U>(); }

        // clang-format on

    private:
        template <Relation Rel, typename R>
        constexpr Comparison<T, R> compare(R&& rhs) {
            bool held = relate<Rel>(std::as_const(value), std::as_const(rhs));
            return Comparison<T, R>{std::forward<T>(value), std::forward<R>(rhs), held};
        }
    };
};

template <typename T>
using Operand = typename Captured<T>::Operand;

/// Starts a check: `Decomposer{} << expr` captures the left operand of a
/// top-level comparison in `expr`, or `expr` itself. Not `<=`: C++20 would also
/// try `expr <=> Decomposer`, and std::tuple's `<=>` breaks on that.
struct Decomposer {
    template <typename T>
    friend constexpr Operand<T> operator<<(Decomposer, T&& value) {
        return Operand<T>{std::forward<T>(value)};
    }
};

/// Reports `split` if it does not hold; returns whether it held.
template <typename Split>
bool check(const Split& split,
           std::string_view expression,
           std::source_location location = std::source_location::current()) {
    if(split.holds()) {
        return true;
    }
    split.fail(expression, location);
    return false;
}

#ifdef __cpp_exceptions

/// Runs `body` and reports whether it threw as `expect_throw` says it should.
void check_throws(function<void()> body,
                  std::string_view expression,
                  bool expect_throw,
                  std::source_location location = std::source_location::current());

#endif

}  // namespace detail

}  // namespace kota::zest
