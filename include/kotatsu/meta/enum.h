#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

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
    return std::array<E, sizeof...(Values)>{enum_probe_value<E, Values>()...};
}

/// The values a probe prints at once: a wider chunk trades a longer signature to split for
/// fewer instantiations.
constexpr std::size_t enum_chunk = 32;

/// The name of the value V of E, empty when no enumerator has it, which the compiler prints as
/// a cast.
template <typename E, int V>
consteval std::string_view probe_name() {
    constexpr E value = enum_probe_value<E, V>();
    constexpr std::string_view name = meta::enum_name<value>();
    return name.find(')') == std::string_view::npos ? name : std::string_view{};
}

/// The name of each of the values Vs of E, empty for one no enumerator has. A chunk the
/// compiler takes no pack of, an enum without a fixed underlying type and values outside its
/// range, is probed a value at a time.
template <typename E, int... Vs>
consteval std::array<std::string_view, sizeof...(Vs)> probe_chunk() {
    std::array<std::string_view, sizeof...(Vs)> names{};
    if constexpr(requires { pack_signature<enum_probe_value<E, Vs>()...>(); }) {
        const auto parts =
            split_pack<sizeof...(Vs)>(pack_signature<enum_probe_value<E, Vs>()...>());
        for(std::size_t i = 0; i < names.size(); ++i) {
            names[i] = parts[i];
        }
    } else {
        std::size_t i = 0;
        ((names[i++] = probe_name<E, Vs>()), ...);
    }
    return names;
}

template <typename E, std::size_t Base, int... Values, std::size_t... Is>
consteval void copy_chunk(std::array<std::string_view, sizeof...(Values)>& names,
                          std::integer_sequence<int, Values...>,
                          std::index_sequence<Is...>) {
    constexpr int values[] = {Values...};
    const auto chunk = probe_chunk<E, values[Base + Is]...>();
    for(std::size_t i = 0; i < chunk.size(); ++i) {
        names[Base + i] = chunk[i];
    }
}

/// The name of each of the values the reflection of E scans, empty for one no enumerator has.
template <typename E, int... Values, std::size_t... Chunks>
consteval auto make_candidate_names(std::integer_sequence<int, Values...> values,
                                    std::index_sequence<Chunks...>) {
    constexpr std::size_t count = sizeof...(Values);
    std::array<std::string_view, count> names{};
    (copy_chunk<E, Chunks * enum_chunk>(
         names,
         values,
         std::make_index_sequence<std::min(enum_chunk, count - Chunks * enum_chunk)>{}),
     ...);
    return names;
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
    constexpr inline static auto candidate_names = detail::make_candidate_names<T>(
        scan_sequence{},
        std::make_index_sequence<(scan_sequence::size() + detail::enum_chunk - 1) /
                                 detail::enum_chunk>{});

    constexpr inline static std::size_t member_count = [] {
        std::size_t count = 0;
        for(std::string_view name: candidate_names) {
            count += !name.empty();
        }
        return count;
    }();
    constexpr inline static auto member_values = [] {
        std::array<T, member_count> out{};
        std::size_t idx = 0;
        for(std::size_t i = 0; i < candidate_names.size(); ++i) {
            if(!candidate_names[i].empty()) {
                out[idx++] = candidate_values[i];
            }
        }
        return out;
    }();
    constexpr inline static auto member_names = [] {
        std::array<std::string_view, member_count> out{};
        std::size_t idx = 0;
        for(std::string_view name: candidate_names) {
            if(!name.empty()) {
                out[idx++] = name;
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
