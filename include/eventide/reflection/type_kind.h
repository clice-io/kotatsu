#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "annotation.h"
#include "struct.h"
#include "eventide/common/meta.h"
#include "eventide/common/ranges.h"

namespace eventide::refl {

template <typename T>
concept null_like = is_one_of<T, std::nullptr_t, std::nullopt_t, std::monostate>;

template <typename T>
concept bool_like = std::same_as<T, bool>;

template <typename T>
concept int_like = is_one_of<T,
                             signed char,
                             short,
                             int,
                             long,
                             long long,
                             std::int8_t,
                             std::int16_t,
                             std::int32_t,
                             std::int64_t>;

template <typename T>
concept uint_like = is_one_of<T,
                              unsigned char,
                              unsigned short,
                              unsigned int,
                              unsigned long,
                              unsigned long long,
                              std::uint8_t,
                              std::uint16_t,
                              std::uint32_t,
                              std::uint64_t>;

template <typename T>
concept floating_like = is_one_of<T, float, double, long double>;

template <typename T>
concept char_like = std::same_as<T, char>;

template <typename T>
concept str_like = std::convertible_to<T, std::string_view>;

template <typename T>
concept bytes_like = std::convertible_to<T, std::span<const std::byte>>;

template <typename T>
constexpr inline bool is_pair_v = is_specialization_of<std::pair, T>;

template <typename T>
constexpr inline bool is_tuple_v = is_specialization_of<std::tuple, T>;

namespace detail {

template <typename T, std::size_t... Is>
consteval bool tuple_gettable_impl(std::index_sequence<Is...>) {
    return (requires(T& value) { std::get<Is>(value); } && ...);
}

template <typename T>
consteval bool tuple_gettable() {
    return tuple_gettable_impl<T>(std::make_index_sequence<std::tuple_size_v<T>>{});
}

}  // namespace detail

template <typename T>
concept tuple_like = requires { typename std::tuple_size<std::remove_cvref_t<T>>::type; } &&
                     detail::tuple_gettable<std::remove_cvref_t<T>>();

enum class type_kind : std::uint8_t {
    null = 0,
    boolean,
    int8,
    int16,
    int32,
    int64,
    uint8,
    uint16,
    uint32,
    uint64,
    float32,
    float64,
    character,
    string,
    bytes,
    enumeration,

    array,
    set,
    map,
    tuple,
    structure,
    variant,
    optional,
    pointer,

    unknown = 254,
    any = 255,
};

enum class tag_mode : std::uint8_t {
    none,
    external,
    internal,
    adjacent,
};

template <typename T>
constexpr inline bool schema_opaque = false;

template <typename T>
consteval type_kind kind_of() {
    if constexpr(!std::is_same_v<T, std::remove_cv_t<T>>) {
        return kind_of<std::remove_cv_t<T>>();
    } else if constexpr(annotated_type<T>) {
        return kind_of<typename T::annotated_type>();
    } else if constexpr(schema_opaque<T>) {
        return type_kind::unknown;
    } else if constexpr(std::is_enum_v<T>) {
        return type_kind::enumeration;
    } else if constexpr(bool_like<T>) {
        return type_kind::boolean;
    } else if constexpr(int_like<T>) {
        return sizeof(T) == 1   ? type_kind::int8
               : sizeof(T) == 2 ? type_kind::int16
               : sizeof(T) == 4 ? type_kind::int32
                                : type_kind::int64;
    } else if constexpr(uint_like<T>) {
        return sizeof(T) == 1   ? type_kind::uint8
               : sizeof(T) == 2 ? type_kind::uint16
               : sizeof(T) == 4 ? type_kind::uint32
                                : type_kind::uint64;
    } else if constexpr(floating_like<T>) {
        return sizeof(T) <= 4 ? type_kind::float32 : type_kind::float64;
    } else if constexpr(char_like<T>) {
        return type_kind::character;
    } else if constexpr(str_like<T>) {
        return type_kind::string;
    } else if constexpr(bytes_like<T>) {
        return type_kind::bytes;
    } else if constexpr(null_like<T>) {
        return type_kind::null;
    } else if constexpr(is_optional_v<T>) {
        return type_kind::optional;
    } else if constexpr(is_specialization_of<std::unique_ptr, T> ||
                        is_specialization_of<std::shared_ptr, T>) {
        return type_kind::pointer;
    } else if constexpr(is_specialization_of<std::variant, T>) {
        return type_kind::variant;
    } else if constexpr(tuple_like<T>) {
        return type_kind::tuple;
    } else if constexpr(std::ranges::input_range<T>) {
        constexpr auto fmt = format_kind<T>;
        if constexpr(fmt == range_format::map) {
            return type_kind::map;
        } else if constexpr(fmt == range_format::set) {
            return type_kind::set;
        } else if constexpr(fmt == range_format::sequence) {
            return type_kind::array;
        } else {
            return type_kind::unknown;
        }
    } else if constexpr(reflectable_class<T>) {
        return type_kind::structure;
    } else {
        return type_kind::unknown;
    }
}

}  // namespace eventide::refl
