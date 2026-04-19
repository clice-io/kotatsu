#pragma once

#include <concepts>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>

#include "type_traits.h"

namespace kota {

namespace detail {

// A type counts as a "map value" if it is tuple-like with exactly two
// elements (e.g. std::pair, std::tuple<K,V>, llvm::StringMapEntry<V>, etc.).
// We intentionally avoid naming std::pair/std::tuple here so that custom
// entry types (like LLVM's StringMapEntry which specializes std::tuple_size /
// std::tuple_element) are accepted without per-type specialization.
template <typename T>
concept map_entry_like = requires {
    { std::tuple_size<T>::value } -> std::convertible_to<std::size_t>;
} && (std::tuple_size<T>::value == 2);

}  // namespace detail

template <typename T>
constexpr bool is_map_value_v = detail::map_entry_like<std::remove_cvref_t<T>>;

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
concept sequence_insertable = requires(Container& container, Element&& element) {
    container.emplace_back(std::forward<Element>(element));
} || requires(Container& container, Element&& element) {
    container.push_back(std::forward<Element>(element));
} || requires(Container& container, Element&& element) {
    container.insert(container.end(), std::forward<Element>(element));
} || requires(Container& container, Element&& element) {
    container.insert(std::forward<Element>(element));
};

template <typename Container, typename Element>
constexpr bool append_sequence_element(Container& container, Element&& element) {
    if constexpr(requires { container.emplace_back(std::forward<Element>(element)); }) {
        container.emplace_back(std::forward<Element>(element));
        return true;
    } else if constexpr(requires { container.push_back(std::forward<Element>(element)); }) {
        container.push_back(std::forward<Element>(element));
        return true;
    } else if constexpr(requires {
                            container.insert(container.end(), std::forward<Element>(element));
                        }) {
        container.insert(container.end(), std::forward<Element>(element));
        return true;
    } else if constexpr(requires { container.insert(std::forward<Element>(element)); }) {
        container.insert(std::forward<Element>(element));
        return true;
    } else {
        return false;
    }
}

template <typename Map, typename Key, typename Mapped>
concept map_insertable = requires(Map& map, Key&& key, Mapped&& value) {
    map.insert_or_assign(std::forward<Key>(key), std::forward<Mapped>(value));
} || requires(Map& map, Key&& key, Mapped&& value) {
    map.emplace(std::forward<Key>(key), std::forward<Mapped>(value));
} || requires(Map& map, Key&& key, Mapped&& value) {
    map.insert(typename Map::value_type{std::forward<Key>(key), std::forward<Mapped>(value)});
};

template <typename Map, typename Key, typename Mapped>
constexpr bool insert_map_entry(Map& map, Key&& key, Mapped&& value) {
    if constexpr(requires {
                     map.insert_or_assign(std::forward<Key>(key), std::forward<Mapped>(value));
                 }) {
        map.insert_or_assign(std::forward<Key>(key), std::forward<Mapped>(value));
        return true;
    } else if constexpr(requires {
                            map.emplace(std::forward<Key>(key), std::forward<Mapped>(value));
                        }) {
        map.emplace(std::forward<Key>(key), std::forward<Mapped>(value));
        return true;
    } else if constexpr(requires {
                            map.insert(typename Map::value_type{std::forward<Key>(key),
                                                                std::forward<Mapped>(value)});
                        }) {
        map.insert(typename Map::value_type{std::forward<Key>(key), std::forward<Mapped>(value)});
        return true;
    } else {
        return false;
    }
}

}  // namespace detail

}  // namespace kota
