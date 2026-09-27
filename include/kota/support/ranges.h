#pragma once

#include <concepts>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>

#include "type_traits.h"

namespace kota {

namespace detail {

// A type counts as a "map value" if either:
//  (a) it is tuple-like with exactly two elements (std::pair, std::tuple<K,V>,
//      llvm::StringMapEntry<V>, ...), or
//  (b) it exposes `.first` / `.second` members — covers types that derive from
//      std::pair without re-specializing std::tuple_size (e.g. llvm::detail::DenseMapPair).
template <typename T>
concept map_entry_tuple_like = requires {
    { std::tuple_size<T>::value } -> std::convertible_to<std::size_t>;
    requires std::tuple_size<T>::value == 2;
};

template <typename T>
concept map_entry_pair_like = requires(T& t) {
    t.first;
    t.second;
};

template <typename T>
concept map_entry_like = map_entry_tuple_like<T> || map_entry_pair_like<T>;

}  // namespace detail

template <typename T>
constexpr bool is_map_value_v = detail::map_entry_like<std::remove_cvref_t<T>>;

// Extract key/mapped types from a map entry reference.
namespace detail {

template <typename E, typename = void>
struct map_entry_types;

template <typename E>
struct map_entry_types<E, std::enable_if_t<map_entry_tuple_like<E>>> {
    using key_type = std::remove_cvref_t<std::tuple_element_t<0, E>>;
    using mapped_type = std::remove_cvref_t<std::tuple_element_t<1, E>>;
};

template <typename E>
struct map_entry_types<E, std::enable_if_t<!map_entry_tuple_like<E> && map_entry_pair_like<E>>> {
    using key_type = std::remove_cvref_t<decltype(std::declval<E&>().first)>;
    using mapped_type = std::remove_cvref_t<decltype(std::declval<E&>().second)>;
};

}  // namespace detail

template <typename E>
using map_entry_key_t = typename detail::map_entry_types<std::remove_cvref_t<E>>::key_type;

template <typename E>
using map_entry_mapped_t = typename detail::map_entry_types<std::remove_cvref_t<E>>::mapped_type;

enum class range_format { disabled, map, set, sequence };

template <class R>
constexpr range_format format_kind = [] {
    static_assert(dependent_false<R>, "instantiating a primary template is not allowed");
    return range_format::disabled;
}();

template <std::ranges::input_range R>
    requires std::same_as<R, std::remove_cvref_t<R>>
constexpr range_format format_kind<R> = [] {
    using ref_t = std::ranges::range_reference_t<R>;
    if constexpr(std::same_as<std::remove_cvref_t<ref_t>, R>) {
        return range_format::disabled;
    } else if constexpr(requires { typename R::key_type; }) {
        if constexpr(requires { typename R::mapped_type; } && is_map_value_v<ref_t>) {
            return range_format::map;
        } else {
            return range_format::set;
        }
    } else {
        return range_format::sequence;
    }
}();

template <typename T, range_format Kind>
concept range_of_kind = [] {
    using U = std::remove_cvref_t<T>;
    if constexpr(std::ranges::input_range<U>) {
        return format_kind<U> == Kind;
    } else {
        return false;
    }
}();

template <typename T>
concept sequence_range = range_of_kind<T, range_format::sequence>;

template <typename T>
concept set_range = range_of_kind<T, range_format::set>;

template <typename T>
concept map_range = range_of_kind<T, range_format::map>;

template <typename T>
concept ordered_associative_range =
    (set_range<T> || map_range<T>) && requires { typename T::key_compare; };

template <typename T>
concept unordered_associative_range = (set_range<T> || map_range<T>) && requires {
    typename T::hasher;
    typename T::key_equal;
};

template <typename T>
concept ordered_set_range = set_range<T> && ordered_associative_range<T>;

template <typename T>
concept ordered_map_range = map_range<T> && ordered_associative_range<T>;

template <typename T>
concept unordered_set_range = set_range<T> && unordered_associative_range<T>;

template <typename T>
concept unordered_map_range = map_range<T> && unordered_associative_range<T>;

namespace detail {

template <typename Container, typename Element>
constexpr void append_sequence_element(Container& container, Element&& element) {
    if constexpr(requires { container.emplace_back(std::forward<Element>(element)); }) {
        container.emplace_back(std::forward<Element>(element));
    } else if constexpr(requires { container.push_back(std::forward<Element>(element)); }) {
        container.push_back(std::forward<Element>(element));
    } else if constexpr(requires {
                            container.insert(container.end(), std::forward<Element>(element));
                        }) {
        container.insert(container.end(), std::forward<Element>(element));
    } else if constexpr(requires { container.insert(std::forward<Element>(element)); }) {
        container.insert(std::forward<Element>(element));
    } else {
        static_assert(dependent_false<Container>,
                      "the container has no emplace_back, push_back or insert to append with");
    }
}

template <typename Map, typename Key, typename Mapped>
constexpr void insert_map_entry(Map& map, Key&& key, Mapped&& value) {
    if constexpr(requires {
                     map.insert_or_assign(std::forward<Key>(key), std::forward<Mapped>(value));
                 }) {
        map.insert_or_assign(std::forward<Key>(key), std::forward<Mapped>(value));
    } else if constexpr(requires {
                            map.emplace(std::forward<Key>(key), std::forward<Mapped>(value));
                        }) {
        map.emplace(std::forward<Key>(key), std::forward<Mapped>(value));
    } else if constexpr(requires {
                            map.insert(typename Map::value_type{std::forward<Key>(key),
                                                                std::forward<Mapped>(value)});
                        }) {
        map.insert(typename Map::value_type{std::forward<Key>(key), std::forward<Mapped>(value)});
    } else {
        static_assert(dependent_false<Map>,
                      "the map has no insert_or_assign, emplace or insert to add an entry with");
    }
}

}  // namespace detail

}  // namespace kota
