#pragma once

#include <algorithm>
#include <exception>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "kota/zest/assert/check.h"
#include "kota/zest/assert/trace.h"
#include "kota/meta/compare.h"
#include "kota/meta/name.h"

// Predicates for checks, e.g. `ZEXPECT(contains(text, "key"))`. Each returns a
// Match, which carries how to show its inputs when the check fails; `!`
// negates one and keeps that. A predicate takes its arguments by reference:
// the explanation reads them, and they live as long as the check.

namespace kota::zest {

namespace detail {

/// What a text predicate looks for: text through as_text, or a character.
template <typename P>
constexpr auto as_pattern(const P& pattern) {
    if constexpr(meta::str_like<P>) {
        return as_text(pattern);
    } else {
        return std::optional<P>(pattern);
    }
}

}  // namespace detail

/// `needle` is in `haystack`: a substring or character of text, or an element
/// of a range.
template <typename H, typename N>
Match contains(const H& haystack, const N& needle) {
    bool held;
    if constexpr(meta::str_like<H>) {
        auto text = detail::as_text(haystack);
        auto pattern = detail::as_pattern(needle);
        held = text && pattern && text->find(*pattern) != std::string_view::npos;
    } else {
        held = std::ranges::any_of(haystack, [&](const auto& element) {
            return detail::relate<detail::Relation::Equal>(element, needle);
        });
    }
    return Match{.held = held, .explain = [&] {
                     return std::format("haystack: {}\nneedle: {}",
                                        pretty_dump(haystack),
                                        pretty_dump(needle));
                 }};
}

template <typename T, typename P>
Match starts_with(const T& text, const P& prefix) {
    auto view = detail::as_text(text);
    auto pattern = detail::as_pattern(prefix);
    return Match{.held = view && pattern && view->starts_with(*pattern), .explain = [&] {
                     return std::format("text: {}\nprefix: {}",
                                        pretty_dump(text),
                                        pretty_dump(prefix));
                 }};
}

template <typename T, typename S>
Match ends_with(const T& text, const S& suffix) {
    auto view = detail::as_text(text);
    auto pattern = detail::as_pattern(suffix);
    return Match{.held = view && pattern && view->ends_with(*pattern), .explain = [&] {
                     return std::format("text: {}\nsuffix: {}",
                                        pretty_dump(text),
                                        pretty_dump(suffix));
                 }};
}

/// `L` and `R` are one type.
template <typename L, typename R>
Match type_eq() {
    return Match{
        .held = std::is_same_v<L, R>,
        .explain =
            [] {
                return std::format("lhs: {}\nrhs: {}", meta::type_name<L>(), meta::type_name<R>());
            },
    };
}

// Plain try and catch: these exist only where exceptions do, and catching by
// type has no KOTA_ macro.
#ifdef __cpp_exceptions

/// Calling `body` throws, an `E` if `E` is given; `!throws(body)` is that it
/// throws nothing. The explanation names what was thrown, if anything.
template <typename E = void, typename F>
Match throws(F&& body) {
    // Kept for the explanation, which only a failing check reads.
    std::exception_ptr thrown;
    bool held = false;
    if constexpr(std::is_void_v<E>) {
        try {
            std::forward<F>(body)();
        } catch(...) {
            thrown = std::current_exception();
            held = true;
        }
    } else {
        try {
            std::forward<F>(body)();
        } catch(const E&) {
            thrown = std::current_exception();
            held = true;
        } catch(...) {
            thrown = std::current_exception();
        }
    }
    return Match{
        .held = held,
        .explain =
            [thrown] {
                auto what = thrown ? std::format("thrown: {}", describe_exception(thrown))
                                   : std::string("nothing was thrown");
                if constexpr(std::is_void_v<E>) {
                    return what;
                } else {
                    return std::format("expected: {}\n{}", meta::type_name<E>(), what);
                }
            },
    };
}

#endif

}  // namespace kota::zest
