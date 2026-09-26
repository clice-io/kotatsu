#pragma once

#include <algorithm>
#include <format>
#include <string_view>
#include <type_traits>

#include "kota/zest/assert/check.h"
#include "kota/meta/compare.h"
#include "kota/meta/name.h"

// Predicates for checks, e.g. `EXPECT(contains(text, "key"))`. Each returns a
// Match, which carries how to show its inputs when the check fails; `!`
// negates one and keeps that. A predicate takes its arguments by reference:
// the explanation reads them, and they live as long as the check.

namespace kota::zest {

/// `needle` is in `haystack`: a substring or character of text, or an element
/// of a range.
template <typename H, typename N>
Match contains(const H& haystack, const N& needle) {
    bool held;
    if constexpr(detail::is_text<H>) {
        held = std::string_view(haystack).find(needle) != std::string_view::npos;
    } else {
        held = std::ranges::any_of(haystack,
                                   [&](const auto& element) { return meta::eq(element, needle); });
    }
    return Match{held, [&] {
                     return std::format("haystack: {}\nneedle: {}",
                                        pretty_dump(haystack),
                                        pretty_dump(needle));
                 }};
}

template <typename T, typename P>
Match starts_with(const T& text, const P& prefix) {
    return Match{std::string_view(text).starts_with(prefix), [&] {
                     return std::format("text: {}\nprefix: {}",
                                        pretty_dump(text),
                                        pretty_dump(prefix));
                 }};
}

template <typename T, typename S>
Match ends_with(const T& text, const S& suffix) {
    return Match{std::string_view(text).ends_with(suffix), [&] {
                     return std::format("text: {}\nsuffix: {}",
                                        pretty_dump(text),
                                        pretty_dump(suffix));
                 }};
}

/// `L` and `R` are one type.
template <typename L, typename R>
Match type_eq() {
    return Match{
        std::is_same_v<L, R>,
        [] { return std::format("lhs: {}\nrhs: {}", meta::type_name<L>(), meta::type_name<R>()); }};
}

}  // namespace kota::zest
