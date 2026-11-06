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
/// ascending order of U, which enum_index's binary search needs: an unsigned U (char too, where
/// it is unsigned) converts the negative ones to its largest values, so they come after 0..127.
/// A bool U has only 0 and 1.
template <typename U>
using scan_values = decltype(make_scan_values<U>(std::make_integer_sequence<int, 256>{}));

/// The values a probe prints at once: a wider chunk trades a longer signature for fewer
/// instantiations.
constexpr std::size_t enum_chunk = 32;

/// The name of the enumerator a value prints as, `ns::E::name`; empty for a value no enumerator
/// has, which prints as a cast ending in a number, `(ns::E)5`.
constexpr std::string_view enumerator(std::string_view printed) {
    const std::string_view name = trailing_identifier(printed);
    return name.empty() || (name[0] >= '0' && name[0] <= '9') ? std::string_view{} : name;
}

/// Whether the values of E print as a list a comma separates: E has a fixed underlying type,
/// which every value of is a value of E, as a compiler takes a template argument outside an
/// enum's range of none other; and its name holds no comma, as one in template arguments would.
template <typename E>
concept listed_by_commas =
    requires { E{0}; } && find_char(spelled_type<E>, ',', 0) == spelled_type<E>.size();

/// The name of value V of E, empty when no enumerator has it. Printed from a variable: clang
/// takes as a template argument a value outside an enum's range that a variable holds, but not
/// the call that makes it, nor a template parameter that holds it.
template <typename E, int V>
consteval std::string_view probe_name() {
    constexpr E value = enum_probe_value<E, V>();
    return enumerator(value_spelling.of(value_signature<value>()));
}

/// The name of each of the values Vs of E, empty for one no enumerator has.
template <typename E, int... Vs>
consteval std::array<std::string_view, sizeof...(Vs)> probe_chunk() {
    std::array<std::string_view, sizeof...(Vs)> names{};
    if constexpr(listed_by_commas<E>) {
        const std::string_view list = pack_list<enum_probe_value<E, Vs>()...>();
        std::size_t begin = 0;
        for(std::string_view& name: names) {
            const std::size_t end = find_char(list, ',', begin);
            name = enumerator(list.substr(begin, end - begin));
            begin = end + 1;
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
consteval auto make_scanned_names(std::integer_sequence<int, Values...> values,
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

template <typename E>
constexpr inline auto scanned_names = make_scanned_names<E>(
    scan_values<std::underlying_type_t<E>>{},
    std::make_index_sequence<(scan_values<std::underlying_type_t<E>>::size() + enum_chunk - 1) /
                             enum_chunk>{});

template <std::size_t N>
consteval std::size_t count_named(const std::array<std::string_view, N>& names) {
    std::size_t count = 0;
    for(std::string_view name: names) {
        count += !name.empty();
    }
    return count;
}

template <typename E, std::size_t N>
struct enumerator_table {
    std::array<E, N> values;
    std::array<std::string_view, N> names;
};

/// The enumerators of E among the values it scans, in their order.
template <typename E, int... Values>
consteval auto make_enumerators(std::integer_sequence<int, Values...>) {
    constexpr const auto& names = scanned_names<E>;
    constexpr E values[] = {enum_probe_value<E, Values>()...};
    enumerator_table<E, count_named(names)> table{};
    std::size_t found = 0;
    for(std::size_t i = 0; i < names.size(); ++i) {
        if(!names[i].empty()) {
            table.values[found] = values[i];
            table.names[found] = names[i];
            ++found;
        }
    }
    return table;
}

template <typename E>
constexpr inline auto enumerators = make_enumerators<E>(scan_values<std::underlying_type_t<E>>{});

/// The places of names in the order of the names themselves.
template <std::size_t N>
consteval std::array<std::size_t, N> sorted_order(const std::array<std::string_view, N>& names) {
    std::array<std::size_t, N> order{};
    for(std::size_t i = 0; i < N; ++i) {
        std::size_t at = i;
        for(; at > 0 && names[i] < names[order[at - 1]]; --at) {
            order[at] = order[at - 1];
        }
        order[at] = i;
    }
    return order;
}

/// The places of E's enumerators in the order of their names, which enum_value halves; made
/// only where a name is looked up.
template <typename E>
constexpr inline auto name_order = sorted_order(enumerators<E>.names);

}  // namespace kotatsu::meta::detail

namespace kotatsu::meta {

/// The values of E's enumerators in -128..127, in ascending order of its underlying type.
template <typename E>
    requires std::is_enum_v<E>
constexpr inline auto enum_values = detail::enumerators<E>.values;

/// The names of E's enumerators, in the order of enum_values.
template <typename E>
    requires std::is_enum_v<E>
constexpr inline auto enum_names = detail::enumerators<E>.names;

/// The position of `e` among enum_values<E>, or their count when no enumerator has its value.
template <typename E>
    requires std::is_enum_v<E>
constexpr std::size_t enum_index(E e) {
    using U = std::underlying_type_t<E>;
    const auto& values = enum_values<E>;
    std::size_t left = 0;
    std::size_t right = values.size();
    while(left < right) {
        const std::size_t mid = left + (right - left) / 2;
        if(static_cast<U>(values[mid]) < static_cast<U>(e)) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    return left < values.size() && values[left] == e ? left : values.size();
}

template <typename E>
    requires std::is_enum_v<E>
constexpr std::string_view enum_name(E e, std::string_view fallback = {}) {
    const auto index = enum_index(e);
    return index < enum_names<E>.size() ? enum_names<E>[index] : fallback;
}

/// The value of E's enumerator named `name`; none when no enumerator has the name.
template <typename E>
    requires std::is_enum_v<E>
constexpr std::optional<E> enum_value(std::string_view name) {
    const auto& order = detail::name_order<E>;
    std::size_t left = 0;
    std::size_t right = order.size();
    while(left < right) {
        const std::size_t mid = left + (right - left) / 2;
        if(enum_names<E>[order[mid]] < name) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    if(left < order.size() && enum_names<E>[order[left]] == name) {
        return enum_values<E>[order[left]];
    }
    return std::nullopt;
}

}  // namespace kotatsu::meta
