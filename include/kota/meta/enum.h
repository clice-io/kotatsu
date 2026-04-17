#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>

#include "name.h"

namespace kota::meta::detail {

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

template <int Offset, typename Seq>
struct shifted_integer_sequence;

template <int Offset, int... Is>
struct shifted_integer_sequence<Offset, std::integer_sequence<int, Is...>> {
    using type = std::integer_sequence<int, (Offset + Is)...>;
};

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

}  // namespace kota::meta::detail

namespace kota::meta {

template <typename T>
struct reflection;

template <typename T>
concept enum_type = std::is_enum_v<T>;

template <enum_type T>
struct reflection<T> {
    constexpr static int scan_min = -128;
    constexpr static int scan_max = 127;
    constexpr static int scan_size = scan_max - scan_min + 1;

    using scan_sequence =
        typename detail::shifted_integer_sequence<scan_min,
                                                  std::make_integer_sequence<int, scan_size>>::type;

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

template <enum_type E>
constexpr std::string_view enum_name(E e, std::string_view fallback = {}) {
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
        return reflection<E>::member_names[left];
    }
    return fallback;
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

}  // namespace kota::meta
