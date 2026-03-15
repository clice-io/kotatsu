#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "eventide/common/meta.h"
#include "eventide/common/ranges.h"
#include "eventide/reflection/struct.h"
#include "eventide/serde/serde/annotation.h"
#include "eventide/serde/serde/traits.h"

namespace eventide::serde::schema {

// ─── type_kind: semantic C++ type categories ────────────────────────────────

enum class type_kind : std::uint8_t {
    null_like   = 0,
    boolean     = 1,
    integer     = 2,
    floating    = 3,
    string      = 4,
    character   = 5,
    bytes       = 6,
    enumeration = 7,

    array       = 10,
    set         = 11,
    map         = 12,
    tuple       = 13,

    structure   = 20,
    variant     = 21,
    optional    = 22,
    pointer     = 23,

    any         = 255,
};

// ─── scalar_kind: fine-grained scalar classification for IDL generators ─────

enum class scalar_kind : std::uint8_t {
    none = 0,
    bool_v,
    int8, int16, int32, int64,
    uint8, uint16, uint32, uint64,
    float32, float64,
    char_v,
};

// ─── type_flags: structural properties ──────────────────────────────────────

enum class type_flags : std::uint16_t {
    none                 = 0,
    is_trivial           = 1 << 0,
    is_standard_layout   = 1 << 1,
    is_trivially_copyable = 1 << 2,
    is_signed            = 1 << 3,
    is_ordered           = 1 << 4,
    has_annotated_fields = 1 << 5,
};

constexpr type_flags operator|(type_flags a, type_flags b) noexcept {
    return static_cast<type_flags>(static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
}

constexpr bool has_flag(type_flags set, type_flags flag) noexcept {
    return (static_cast<std::uint16_t>(set) & static_cast<std::uint16_t>(flag)) != 0;
}

// ─── kind_of<T>() ───────────────────────────────────────────────────────────

template <typename T>
consteval type_kind kind_of() {
    using U = std::remove_cvref_t<T>;

    if constexpr(serde::annotated_type<U>) {
        return kind_of<typename U::annotated_type>();
    } else if constexpr(is_specialization_of<std::optional, U>) {
        return type_kind::optional;
    } else if constexpr(serde::null_like<U>) {
        return type_kind::null_like;
    } else if constexpr(serde::bool_like<U>) {
        return type_kind::boolean;
    } else if constexpr(std::is_enum_v<U>) {
        return type_kind::enumeration;
    } else if constexpr(serde::int_like<U> || serde::uint_like<U>) {
        return type_kind::integer;
    } else if constexpr(serde::floating_like<U>) {
        return type_kind::floating;
    } else if constexpr(serde::char_like<U>) {
        return type_kind::character;
    } else if constexpr(serde::str_like<U>) {
        return type_kind::string;
    } else if constexpr(is_specialization_of<std::variant, U>) {
        return type_kind::variant;
    } else if constexpr(serde::bytes_like<U>) {
        return type_kind::bytes;
    } else if constexpr(serde::is_pair_v<U> || serde::is_tuple_v<U>) {
        return type_kind::tuple;
    } else if constexpr(is_specialization_of<std::unique_ptr, U> ||
                        is_specialization_of<std::shared_ptr, U>) {
        return type_kind::pointer;
    } else if constexpr(std::ranges::input_range<U>) {
        constexpr auto fmt = format_kind<U>;
        if constexpr(fmt == range_format::map) {
            return type_kind::map;
        } else if constexpr(fmt == range_format::set) {
            return type_kind::set;
        } else {
            return type_kind::array;
        }
    } else if constexpr(refl::reflectable_class<U>) {
        return type_kind::structure;
    } else {
        return type_kind::any;
    }
}

// ─── scalar_kind_of<T>() ────────────────────────────────────────────────────

template <typename T>
consteval scalar_kind scalar_kind_of() {
    using U = std::remove_cvref_t<T>;
    if constexpr(std::same_as<U, bool>) return scalar_kind::bool_v;
    else if constexpr(std::same_as<U, char>) return scalar_kind::char_v;
    else if constexpr(std::same_as<U, std::int8_t> || std::same_as<U, signed char>)
        return scalar_kind::int8;
    else if constexpr(std::same_as<U, std::int16_t> || std::same_as<U, short>)
        return scalar_kind::int16;
    else if constexpr(std::same_as<U, std::int32_t> || std::same_as<U, int>)
        return scalar_kind::int32;
    else if constexpr(std::same_as<U, std::int64_t> || std::same_as<U, long long> ||
                      std::same_as<U, long>)
        return scalar_kind::int64;
    else if constexpr(std::same_as<U, std::uint8_t> || std::same_as<U, unsigned char>)
        return scalar_kind::uint8;
    else if constexpr(std::same_as<U, std::uint16_t> || std::same_as<U, unsigned short>)
        return scalar_kind::uint16;
    else if constexpr(std::same_as<U, std::uint32_t> || std::same_as<U, unsigned int> ||
                      std::same_as<U, unsigned long>)
        return scalar_kind::uint32;
    else if constexpr(std::same_as<U, std::uint64_t> || std::same_as<U, unsigned long long>)
        return scalar_kind::uint64;
    else if constexpr(std::same_as<U, float>) return scalar_kind::float32;
    else if constexpr(std::same_as<U, double>) return scalar_kind::float64;
    else if constexpr(std::is_enum_v<U>) return scalar_kind_of<std::underlying_type_t<U>>();
    else return scalar_kind::none;
}

// ─── type_flags_of<T>() ─────────────────────────────────────────────────────

namespace detail {

template <typename T>
consteval bool check_annotated_fields() {
    if constexpr(!refl::reflectable_class<std::remove_cvref_t<T>>) {
        return false;
    } else {
        using U = std::remove_cvref_t<T>;
        return []<std::size_t... Is>(std::index_sequence<Is...>) consteval {
            return (serde::annotated_type<refl::field_type<U, Is>> || ...);
        }(std::make_index_sequence<refl::field_count<U>()>{});
    }
}

}  // namespace detail

template <typename T>
consteval type_flags type_flags_of() {
    using U = std::remove_cvref_t<T>;
    type_flags f = type_flags::none;
    if constexpr(std::is_trivial_v<U>) f = f | type_flags::is_trivial;
    if constexpr(std::is_standard_layout_v<U>) f = f | type_flags::is_standard_layout;
    if constexpr(std::is_trivially_copyable_v<U>) f = f | type_flags::is_trivially_copyable;
    if constexpr(std::is_signed_v<U>) f = f | type_flags::is_signed;
    if constexpr(std::ranges::input_range<U>) {
        if constexpr(requires { typename U::key_type; }) {
            if constexpr(!is_specialization_of<std::unordered_map, U> &&
                         !is_specialization_of<std::unordered_set, U>)
                f = f | type_flags::is_ordered;
        }
    }
    if constexpr(detail::check_annotated_fields<U>()) f = f | type_flags::has_annotated_fields;
    return f;
}

// ─── tag_mode: variant tagging strategy ─────────────────────────────────────

enum class tag_mode : std::uint8_t {
    none = 0,
    external = 1,
    internal = 2,
    adjacent = 3,
};

// ─── is_optional_type<T>() ──────────────────────────────────────────────────

template <typename T>
consteval bool is_optional_type() {
    using U = std::remove_cvref_t<T>;
    if constexpr(serde::annotated_type<U>) {
        return is_optional_type<typename U::annotated_type>();
    } else {
        return is_optional_v<U>;
    }
}

// ─── hint_to_kind: wire format → C++ type mapping ──────────────────────────

constexpr type_kind hint_to_kind(std::uint8_t hint_bits) {
    if(hint_bits & 0x40) return type_kind::structure;
    if(hint_bits & 0x20) return type_kind::array;
    if(hint_bits & 0x10) return type_kind::string;
    if(hint_bits & 0x08) return type_kind::floating;
    if(hint_bits & 0x04) return type_kind::integer;
    if(hint_bits & 0x02) return type_kind::boolean;
    if(hint_bits & 0x01) return type_kind::null_like;
    return type_kind::any;
}

// ─── kind_to_hint_index: maps type_kind to type_hint bit position ───────────

consteval std::size_t kind_to_hint_index(type_kind k) {
    switch(k) {
    case type_kind::null_like: return 0;
    case type_kind::boolean: return 1;
    case type_kind::integer:
    case type_kind::enumeration: return 2;
    case type_kind::floating: return 3;
    case type_kind::string:
    case type_kind::character: return 4;
    case type_kind::array:
    case type_kind::set:
    case type_kind::tuple:
    case type_kind::bytes: return 5;
    case type_kind::structure:
    case type_kind::map: return 6;
    default: return 7;
    }
}

}  // namespace eventide::serde::schema

namespace eventide::serde {

/// Bitmask of data-model type categories.
/// Backends map their format-specific "kind" enums to these bits.
enum class type_hint : std::uint8_t {
    null_like = 1 << 0,
    boolean = 1 << 1,
    integer = 1 << 2,
    floating = 1 << 3,
    string = 1 << 4,
    array = 1 << 5,
    object = 1 << 6,
    any = 0x7F,
};

constexpr type_hint operator|(type_hint a, type_hint b) noexcept {
    return static_cast<type_hint>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

constexpr bool has_any(type_hint set, type_hint flags) noexcept {
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(flags)) != 0;
}

}  // namespace eventide::serde
