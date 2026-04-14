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

// ---------------------------------------------------------------------------
// type_kind — unified type classification
// ---------------------------------------------------------------------------

/// Unified type classification replacing the old type_kind + scalar_kind pair.
/// Scalars use fine-grained values; compound types use broad categories.
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

// ---------------------------------------------------------------------------
// tag_mode — variant tagging strategy
// ---------------------------------------------------------------------------

enum class tag_mode : std::uint8_t {
    none,
    external,
    internal,
    adjacent,
};

// ---------------------------------------------------------------------------
// type_info — base class and subtypes
// ---------------------------------------------------------------------------

/// Base type descriptor. Scalars use this directly; compound types downcast
/// to the appropriate subclass via `kind`.
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

/// array / set
struct array_type_info : type_info {
    const type_info* element;
};

/// map
struct map_type_info : type_info {
    const type_info* key;
    const type_info* value;
};

/// enum — runtime-accessible enum metadata
struct enum_type_info : type_info {
    std::span<const std::string_view> member_names;
    std::span<const std::int64_t> member_values;
    type_kind underlying_kind;
};

/// tuple / pair
struct tuple_type_info : type_info {
    std::span<const type_info* const> elements;
};

/// variant — includes tagging metadata
struct variant_type_info : type_info {
    std::span<const type_info* const> alternatives;
    tag_mode tagging = tag_mode::none;
    std::string_view tag_field;
    std::string_view content_field;
    std::span<const std::string_view> alt_names;
};

/// optional / smart_ptr — wraps the inner type
struct optional_type_info : type_info {
    const type_info* inner;
};

// ---------------------------------------------------------------------------
// field_info
// ---------------------------------------------------------------------------

struct field_info {
    std::string_view name;                      // canonical wire name
    std::span<const std::string_view> aliases;  // alias names
    std::size_t offset;                         // byte offset from struct start
    std::size_t physical_index;                 // original C++ struct field index
    const type_info* type;                      // recursive type descriptor (wire view)

    // Level 1 flags
    bool has_default;   // schema::default_value
    bool is_literal;    // schema::literal
    bool has_skip_if;   // behavior::skip_if present
    bool has_behavior;  // with/as/enum_string present
};

/// struct — placed after field_info so that std::span<const field_info> sees a complete type.
struct struct_type_info : type_info {
    std::span<const field_info> fields;
    bool is_trivial_layout;
};

// ---------------------------------------------------------------------------
// schema_opaque — opt-out from recursive type decomposition
// ---------------------------------------------------------------------------

/// Types marked schema_opaque are treated as opaque by the schema system.
/// They get kind=unknown and are not recursively decomposed in type_info_instance.
/// Backend-specific serialize/deserialize hooks handle these types directly.
template <typename T>
constexpr inline bool schema_opaque = false;

// ---------------------------------------------------------------------------
// kind_of<T>() — map C++ types to type_kind values
// ---------------------------------------------------------------------------

namespace detail {

/// Map signed integer types to their exact-width type_kind.
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

/// Map unsigned integer types to their exact-width type_kind.
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

/// Map floating-point types to their type_kind.
template <typename T>
consteval type_kind floating_kind() {
    if constexpr(sizeof(T) <= 4) {
        return type_kind::float32;
    } else {
        return type_kind::float64;
    }
}

}  // namespace detail

/// Map a C++ type to its type_kind value.
///
/// Scalars yield fine-grained kinds (int8, float64, etc.);
/// compound types yield broad categories (array, map, structure, etc.).
///
/// The ordering of checks mirrors the dispatch chain in serde.h:
///   annotated_type -> enum -> bool -> int -> uint -> float -> char ->
///   str -> bytes -> null -> optional -> pointer -> variant -> tuple ->
///   range -> reflectable_class
template <typename T>
consteval type_kind kind_of() {
    using V = std::remove_cvref_t<T>;

    // Unwrap annotation to get the underlying type
    if constexpr(serde::annotated_type<V>) {
        return kind_of<typename V::annotated_type>();
    }
    // Opaque types with custom hooks — do not decompose
    else if constexpr(schema_opaque<V>) {
        return type_kind::unknown;
    }
    // Enum -> enumeration
    else if constexpr(std::is_enum_v<V>) {
        return type_kind::enumeration;
    }
    // Bool
    else if constexpr(serde::bool_like<V>) {
        return type_kind::boolean;
    }
    // Signed integers — size-based dispatch
    else if constexpr(serde::int_like<V>) {
        return detail::signed_int_kind<V>();
    }
    // Unsigned integers — size-based dispatch
    else if constexpr(serde::uint_like<V>) {
        return detail::unsigned_int_kind<V>();
    }
    // Floating-point
    else if constexpr(serde::floating_like<V>) {
        return detail::floating_kind<V>();
    }
    // Character
    else if constexpr(serde::char_like<V>) {
        return type_kind::character;
    }
    // String
    else if constexpr(serde::str_like<V>) {
        return type_kind::string;
    }
    // Bytes
    else if constexpr(serde::bytes_like<V>) {
        return type_kind::bytes;
    }
    // Null
    else if constexpr(serde::null_like<V>) {
        return type_kind::null;
    }
    // Optional
    else if constexpr(is_optional_v<V>) {
        return type_kind::optional;
    }
    // Smart pointers
    else if constexpr(is_specialization_of<std::unique_ptr, V> ||
                      is_specialization_of<std::shared_ptr, V>) {
        return type_kind::pointer;
    }
    // Variant
    else if constexpr(is_specialization_of<std::variant, V>) {
        return type_kind::variant;
    }
    // Tuple / pair (before range, since some tuples might satisfy range)
    else if constexpr(serde::tuple_like<V>) {
        return type_kind::tuple;
    }
    // Range types: map / set / sequence
    else if constexpr(std::ranges::input_range<V>) {
        constexpr auto fmt = format_kind<V>;
        if constexpr(fmt == range_format::map) {
            return type_kind::map;
        } else if constexpr(fmt == range_format::set) {
            return type_kind::set;
        } else {
            return type_kind::array;
        }
    }
    // Reflectable struct
    else if constexpr(refl::reflectable_class<V>) {
        return type_kind::structure;
    }
    // Unknown — no matching category
    else {
        return type_kind::unknown;
    }
}

}  // namespace eventide::serde::schema
