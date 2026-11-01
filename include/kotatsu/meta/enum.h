#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>

#include "name.h"

namespace kotatsu::meta::detail {

template <typename E, int V>
consteval E enum_probe_value() {
#if defined(__clang__) && __clang_major__ >= 16
    return std::bit_cast<E>(static_cast<std::underlying_type_t<E>>(V));
#else
    return static_cast<E>(V);
#endif
}

template <typename E, int V>
consteval std::string_view enum_probe_name() {
    constexpr E value = enum_probe_value<E, V>();
    return meta::enum_name<value>();
}

template <typename E, int V>
struct enum_probe {
    constexpr static E value = enum_probe_value<E, V>();
    constexpr static std::string_view name = enum_probe_name<E, V>();
    constexpr static bool valid = !name.empty() && name.find(')') == std::string_view::npos;
};

template <typename U, int... Is>
consteval auto make_scan_values(std::integer_sequence<int, Is...>) {
    if constexpr(std::is_same_v<U, bool>) {
        // Any other value converts to true, which would name the true member again.
        return std::integer_sequence<int, 0, 1>{};
    } else if constexpr(std::is_signed_v<U>) {
        return std::integer_sequence<int, (Is - 128)...>{};
    } else {
        return std::integer_sequence<int, (Is < 128 ? Is : Is - 256)...>{};
    }
}

/// The values the reflection of an enum whose underlying type is U scans, -128..127, in
/// ascending order of U, which enum_name's binary search needs: an unsigned U (char too, where
/// it is unsigned) converts the negative ones to its largest values, so they come after 0..127.
/// A bool U has only 0 and 1.
template <typename U>
using scan_values = decltype(make_scan_values<U>(std::make_integer_sequence<int, 256>{}));

template <typename E, int... Values>
consteval auto make_candidate_values(std::integer_sequence<int, Values...>) {
    return std::array<E, sizeof...(Values)>{enum_probe<E, Values>::value...};
}

template <typename E, int... Values>
consteval auto make_candidate_names(std::integer_sequence<int, Values...>) {
    return std::array<std::string_view, sizeof...(Values)>{enum_probe<E, Values>::name...};
}

template <typename E, int... Values>
consteval auto make_candidate_valid(std::integer_sequence<int, Values...>) {
    return std::array<bool, sizeof...(Values)>{enum_probe<E, Values>::valid...};
}

template <std::size_t N>
consteval auto count_valid(const std::array<bool, N>& valid) {
    std::size_t count = 0;
    for(bool v: valid) {
        if(v) {
            ++count;
        }
    }
    return count;
}

}  // namespace kotatsu::meta::detail

namespace kotatsu::meta {

template <typename T>
struct reflection;

template <typename T>
concept enum_type = std::is_enum_v<T>;

template <enum_type T>
struct reflection<T> {
    using scan_sequence = detail::scan_values<std::underlying_type_t<T>>;

    constexpr inline static auto candidate_values =
        detail::make_candidate_values<T>(scan_sequence{});
    constexpr inline static auto candidate_names = detail::make_candidate_names<T>(scan_sequence{});
    constexpr inline static auto candidate_valid = detail::make_candidate_valid<T>(scan_sequence{});

    constexpr inline static auto member_count = detail::count_valid(candidate_valid);
    constexpr inline static auto member_values = [] {
        std::array<T, member_count> out{};
        std::size_t idx = 0;
        for(std::size_t i = 0; i < candidate_valid.size(); ++i) {
            if(candidate_valid[i]) {
                out[idx++] = candidate_values[i];
            }
        }
        return out;
    }();
    constexpr inline static auto member_names = [] {
        std::array<std::string_view, member_count> out{};
        std::size_t idx = 0;
        for(std::size_t i = 0; i < candidate_valid.size(); ++i) {
            if(candidate_valid[i]) {
                out[idx++] = candidate_names[i];
            }
        }
        return out;
    }();
};

/// The position of `e` among reflection<E>::member_values, or member_count when no enumerator
/// has its value.
template <enum_type E>
constexpr std::size_t enum_index(E e) {
    using U = std::underlying_type_t<E>;
    const auto target = static_cast<U>(e);
    const auto& values = reflection<E>::member_values;

    std::size_t left = 0;
    std::size_t right = values.size();
    while(left < right) {
        const auto mid = left + (right - left) / 2;
        if(static_cast<U>(values[mid]) < target) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    if(left < values.size() && static_cast<U>(values[left]) == target) {
        return left;
    }
    return values.size();
}

template <enum_type E>
constexpr std::string_view enum_name(E e, std::string_view fallback = {}) {
    const auto index = enum_index(e);
    return index < reflection<E>::member_count ? reflection<E>::member_names[index] : fallback;
}

template <enum_type E>
constexpr std::optional<E> enum_value(std::string_view name) {
    const auto& values = reflection<E>::member_values;
    const auto& names = reflection<E>::member_names;
    for(std::size_t index = 0; index < names.size(); ++index) {
        if(names[index] == name) {
            return values[index];
        }
    }
    return std::nullopt;
}

}  // namespace kotatsu::meta
