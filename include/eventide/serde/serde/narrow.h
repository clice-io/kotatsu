#pragma once

#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

#include "eventide/serde/serde/traits.h"

namespace eventide::serde::detail {

/// Narrow int64_t to a smaller signed integer type. Returns error if out of range.
template <int_like T, typename E>
auto narrow_int(std::int64_t parsed, E range_error) -> std::expected<T, E> {
    if(!std::in_range<T>(parsed)) {
        return std::unexpected(range_error);
    }
    return static_cast<T>(parsed);
}

/// Narrow uint64_t to a smaller unsigned integer type. Returns error if out of range.
template <uint_like T, typename E>
auto narrow_uint(std::uint64_t parsed, E range_error) -> std::expected<T, E> {
    if(!std::in_range<T>(parsed)) {
        return std::unexpected(range_error);
    }
    return static_cast<T>(parsed);
}

/// Narrow double to a smaller floating-point type. Checks finite range.
/// For double→double this is a no-op pass-through.
template <floating_like T, typename E>
auto narrow_float(double parsed, E range_error) -> std::expected<T, E> {
    if constexpr(!std::same_as<T, double>) {
        if(std::isfinite(parsed)) {
            const auto low = static_cast<long double>((std::numeric_limits<T>::lowest)());
            const auto high = static_cast<long double>((std::numeric_limits<T>::max)());
            const auto v = static_cast<long double>(parsed);
            if(v < low || v > high) {
                return std::unexpected(range_error);
            }
        }
    }
    return static_cast<T>(parsed);
}

/// Extract a single char from a string_view. Returns error if not exactly 1 character.
template <typename E>
auto narrow_char(std::string_view text, E type_error) -> std::expected<char, E> {
    if(text.size() != 1) {
        return std::unexpected(type_error);
    }
    return text.front();
}

}  // namespace eventide::serde::detail
