#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include "eventide/common/meta.h"
#include "eventide/common/ranges.h"
#include "eventide/reflection/struct.h"
#include "eventide/serde/serde/annotation.h"
#include "eventide/serde/serde/traits.h"

namespace eventide::serde::schema {

struct field_info;

enum class type_kind : std::uint8_t {
    // Scalars (fine-grained)
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

    // Compound types
    array,
    set,
    map,
    tuple,
    structure,
    variant,
    optional,
    pointer,

    // Special
    unknown = 254,  // traits hook / wire representation unknowable
    any = 255,      // future: runtime dynamic type
};

enum class tag_mode : std::uint8_t {
    none,
    external,
    internal,
    adjacent,
};

struct type_info {
    type_kind kind;
    std::string_view type_name;

    constexpr bool is_integer() const {
        return kind >= type_kind::int8 && kind <= type_kind::uint64;
    }

    constexpr bool is_signed_integer() const {
        return kind >= type_kind::int8 && kind <= type_kind::int64;
    }

    constexpr bool is_unsigned_integer() const {
        return kind >= type_kind::uint8 && kind <= type_kind::uint64;
    }

    constexpr bool is_floating() const {
        return kind == type_kind::float32 || kind == type_kind::float64;
    }

    constexpr bool is_numeric() const {
        return is_integer() || is_floating();
    }

    constexpr bool is_scalar() const {
        return kind <= type_kind::enumeration;
    }
};

struct array_type_info : type_info {
    const type_info* element;
};

struct map_type_info : type_info {
    const type_info* key;
    const type_info* value;
};

struct enum_type_info : type_info {
    std::span<const std::string_view> member_names;
    std::span<const std::int64_t> member_values;
    std::span<const std::uint64_t> member_u64_values;
    type_kind underlying_kind;
};

struct tuple_type_info : type_info {
    std::span<const type_info* const> elements;
};

struct variant_type_info : type_info {
    std::span<const type_info* const> alternatives;
    tag_mode tagging = tag_mode::none;
    std::string_view tag_field;
    std::string_view content_field;
    std::span<const std::string_view> alt_names;
};

struct optional_type_info : type_info {
    const type_info* inner;
};

struct field_info {
    std::string_view name;
    std::span<const std::string_view> aliases;
    std::size_t offset;
    std::size_t physical_index;
    const type_info* type;

    bool has_default;
    bool is_literal;
    bool has_skip_if;
    bool has_behavior;
};

struct struct_type_info : type_info {
    std::span<const field_info> fields;
    bool is_trivial_layout;
    bool deny_unknown;
};

template <typename T>
constexpr inline bool schema_opaque = false;

namespace detail {

template <typename T>
consteval type_kind signed_int_kind() {
    if constexpr(sizeof(T) == 1) {
        return type_kind::int8;
    } else if constexpr(sizeof(T) == 2) {
        return type_kind::int16;
    } else if constexpr(sizeof(T) == 4) {
        return type_kind::int32;
    } else {
        static_assert(sizeof(T) == 8);
        return type_kind::int64;
    }
}

template <typename T>
consteval type_kind unsigned_int_kind() {
    if constexpr(sizeof(T) == 1) {
        return type_kind::uint8;
    } else if constexpr(sizeof(T) == 2) {
        return type_kind::uint16;
    } else if constexpr(sizeof(T) == 4) {
        return type_kind::uint32;
    } else {
        static_assert(sizeof(T) == 8);
        return type_kind::uint64;
    }
}

template <typename T>
consteval type_kind floating_kind() {
    if constexpr(sizeof(T) <= 4) {
        return type_kind::float32;
    } else {
        return type_kind::float64;
    }
}

}  // namespace detail

template <typename T>
consteval type_kind kind_of() {
    if constexpr(!std::is_same_v<T, std::remove_cv_t<T>>) {
        return kind_of<std::remove_cv_t<T>>();
    } else if constexpr(serde::annotated_type<T>) {
        return kind_of<typename T::annotated_type>();
    } else if constexpr(schema_opaque<T>) {
        return type_kind::unknown;
    } else if constexpr(std::is_enum_v<T>) {
        return type_kind::enumeration;
    } else if constexpr(serde::bool_like<T>) {
        return type_kind::boolean;
    } else if constexpr(serde::int_like<T>) {
        return detail::signed_int_kind<T>();
    } else if constexpr(serde::uint_like<T>) {
        return detail::unsigned_int_kind<T>();
    } else if constexpr(serde::floating_like<T>) {
        return detail::floating_kind<T>();
    } else if constexpr(serde::char_like<T>) {
        return type_kind::character;
    } else if constexpr(serde::str_like<T>) {
        return type_kind::string;
    } else if constexpr(serde::bytes_like<T>) {
        return type_kind::bytes;
    } else if constexpr(serde::null_like<T>) {
        return type_kind::null;
    } else if constexpr(is_optional_v<T>) {
        return type_kind::optional;
    } else if constexpr(is_specialization_of<std::unique_ptr, T> ||
                        is_specialization_of<std::shared_ptr, T>) {
        return type_kind::pointer;
    } else if constexpr(is_specialization_of<std::variant, T>) {
        return type_kind::variant;
    } else if constexpr(serde::tuple_like<T>) {
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
    } else if constexpr(refl::reflectable_class<T>) {
        return type_kind::structure;
    } else {
        return type_kind::unknown;
    }
}

}  // namespace eventide::serde::schema
