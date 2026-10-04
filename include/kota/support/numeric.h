#pragma once

#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace kota {

/// Narrowing integer cast: returns true and sets `out` if `value` fits in Target.
template <typename Target, typename Source>
    requires std::integral<Target> && (!std::is_const_v<Target>) && std::integral<Source>
constexpr bool narrow_int(Source value, Target& out) {
    static_assert(sizeof(Target) <= sizeof(Source), "not a narrowing conversion");

    bool in_range;
    if constexpr(std::same_as<Target, bool>) {
        in_range = (value == 0) || (value == 1);
    } else if constexpr(std::is_signed_v<Source> == std::is_signed_v<Target>) {
        in_range = value >= (std::numeric_limits<Target>::min)() &&
                   value <= (std::numeric_limits<Target>::max)();
    } else if constexpr(std::is_signed_v<Source>) {
        in_range = value >= 0 && static_cast<std::make_unsigned_t<Source>>(value) <=
                                     static_cast<std::make_unsigned_t<Target>>(
                                         (std::numeric_limits<Target>::max)());
    } else {
        in_range =
            value <=
            static_cast<std::make_unsigned_t<Source>>(
                static_cast<std::make_unsigned_t<Target>>((std::numeric_limits<Target>::max)()));
    }

    if(!in_range) {
        return false;
    }
    out = static_cast<Target>(value);
    return true;
}

/// i <=> d exactly, beyond the 53 bits a double holds; unordered when d is
/// NaN.
template <typename Integer>
    requires std::integral<Integer> && (!std::same_as<Integer, bool>)
constexpr std::partial_ordering compare_exact(Integer i, double d) {
    if(d != d) {
        return std::partial_ordering::unordered;
    }
    // Every Integer lies in [-2^63, 2^64), both ends exact as doubles.
    if(d >= 18446744073709551616.0) {
        return std::partial_ordering::less;
    }
    if(d < -9223372036854775808.0) {
        return std::partial_ordering::greater;
    }
    // d's integer part, toward zero, then the fraction it leaves.
    if(d >= 0) {
        auto whole = static_cast<std::uint64_t>(d);
        if(std::cmp_not_equal(i, whole)) {
            return std::cmp_less(i, whole) ? std::partial_ordering::less
                                           : std::partial_ordering::greater;
        }
        return d > static_cast<double>(whole) ? std::partial_ordering::less
                                              : std::partial_ordering::equivalent;
    }
    auto whole = static_cast<std::int64_t>(d);
    if(std::cmp_not_equal(i, whole)) {
        return std::cmp_less(i, whole) ? std::partial_ordering::less
                                       : std::partial_ordering::greater;
    }
    return d < static_cast<double>(whole) ? std::partial_ordering::greater
                                          : std::partial_ordering::equivalent;
}

}  // namespace kota
