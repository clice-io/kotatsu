#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>

#include "eventide/common/meta.h"
#include "eventide/common/ranges.h"
#include "eventide/reflection/enum.h"
#include "eventide/reflection/struct.h"
#include "eventide/serde/schema/field_info.h"
#include "eventide/serde/schema/field_slot.h"
#include "eventide/serde/serde/annotation.h"
#include "eventide/serde/serde/attrs/behavior.h"
#include "eventide/serde/serde/attrs/schema.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/serde/traits.h"

namespace eventide::serde::schema {

// ===================================================================
// Forward declarations
// ===================================================================

template <typename T, typename Config = serde::config::default_config>
constexpr const type_info* type_info_of();

template <typename T, typename Config = serde::config::default_config>
    requires refl::reflectable_class<std::remove_cvref_t<T>>
struct virtual_schema;

namespace detail {

// ===================================================================
// Constexpr rename utilities (for rename_all support)
// ===================================================================

/// Fixed-capacity constexpr string buffer for compile-time rename transformations.
struct name_buf {
    std::array<char, 64> data{};
    std::size_t len = 0;

    constexpr void push(char c) {
        data[len++] = c;
    }

    constexpr std::string_view view() const {
        return {data.data(), len};
    }
};

constexpr bool cx_is_lower(char c) {
    return c >= 'a' && c <= 'z';
}

constexpr bool cx_is_upper(char c) {
    return c >= 'A' && c <= 'Z';
}

constexpr bool cx_is_digit(char c) {
    return c >= '0' && c <= '9';
}

constexpr bool cx_is_alpha(char c) {
    return cx_is_lower(c) || cx_is_upper(c);
}

constexpr bool cx_is_alnum(char c) {
    return cx_is_alpha(c) || cx_is_digit(c);
}

constexpr char cx_lower(char c) {
    return cx_is_upper(c) ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr char cx_upper(char c) {
    return cx_is_lower(c) ? static_cast<char>(c - 'a' + 'A') : c;
}

/// Normalize any identifier to lower_snake_case at compile time.
/// Handles camelCase, PascalCase, UPPER_SNAKE, and mixed forms.
constexpr name_buf normalize_to_lower_snake_cx(std::string_view text) {
    name_buf out{};
    for(std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if(cx_is_alnum(c)) {
            if(cx_is_upper(c)) {
                const bool prev_alnum = i > 0 && cx_is_alnum(text[i - 1]);
                const bool prev_lower_or_digit =
                    i > 0 && (cx_is_lower(text[i - 1]) || cx_is_digit(text[i - 1]));
                const bool next_lower = i + 1 < text.size() && cx_is_lower(text[i + 1]);
                if(out.len > 0 && out.data[out.len - 1] != '_' && prev_alnum &&
                   (prev_lower_or_digit || next_lower)) {
                    out.push('_');
                }
                out.push(cx_lower(c));
            } else {
                out.push(cx_lower(c));
            }
        } else if(out.len > 0 && out.data[out.len - 1] != '_') {
            out.push('_');
        }
    }
    // Trim leading/trailing underscores.
    std::size_t start = 0;
    while(start < out.len && out.data[start] == '_')
        ++start;
    std::size_t end = out.len;
    while(end > start && out.data[end - 1] == '_')
        --end;
    if(start > 0 || end < out.len) {
        name_buf trimmed{};
        for(std::size_t i = start; i < end; ++i)
            trimmed.push(out.data[i]);
        return trimmed;
    }
    return out;
}

/// Convert a lower_snake identifier to camelCase or PascalCase at compile time.
constexpr name_buf snake_to_camel_cx(std::string_view text, bool upper_first) {
    auto snake = normalize_to_lower_snake_cx(text);
    name_buf out{};
    bool capitalize_next = upper_first;
    bool seen_output = false;
    for(std::size_t i = 0; i < snake.len; ++i) {
        const char c = snake.data[i];
        if(c == '_') {
            capitalize_next = true;
            continue;
        }
        if(capitalize_next && cx_is_alpha(c)) {
            out.push(cx_upper(c));
        } else if(!seen_output) {
            out.push(upper_first ? cx_upper(c) : cx_lower(c));
        } else {
            out.push(c);
        }
        capitalize_next = false;
        seen_output = true;
    }
    return out;
}

/// Convert to UPPER_SNAKE_CASE at compile time.
constexpr name_buf snake_to_upper_cx(std::string_view text) {
    auto snake = normalize_to_lower_snake_cx(text);
    for(std::size_t i = 0; i < snake.len; ++i) {
        snake.data[i] = cx_upper(snake.data[i]);
    }
    return snake;
}

/// Apply the given rename policy at compile time, returning a name_buf.
template <typename Policy>
constexpr name_buf apply_rename_cx(std::string_view input) {
    using namespace serde::spelling::rename_policy;
    if constexpr(std::is_same_v<Policy, identity>) {
        name_buf out{};
        for(std::size_t i = 0; i < input.size(); ++i)
            out.push(input[i]);
        return out;
    } else if constexpr(std::is_same_v<Policy, lower_snake>) {
        return normalize_to_lower_snake_cx(input);
    } else if constexpr(std::is_same_v<Policy, lower_camel>) {
        return snake_to_camel_cx(input, false);
    } else if constexpr(std::is_same_v<Policy, upper_camel>) {
        return snake_to_camel_cx(input, true);
    } else if constexpr(std::is_same_v<Policy, upper_snake>) {
        return snake_to_upper_cx(input);
    } else {
        // Fallback: identity
        name_buf out{};
        for(std::size_t i = 0; i < input.size(); ++i)
            out.push(input[i]);
        return out;
    }
}

/// Static storage for a compile-time renamed field name.
/// Two-step consteval: first compute the length, then store into an exact-size array.
template <typename T, std::size_t I, typename Policy>
struct wire_name_static {
    constexpr static auto buf = apply_rename_cx<Policy>(refl::field_name<I, T>());

    constexpr static auto storage = [] {
        std::array<char, buf.len> arr{};
        for(std::size_t i = 0; i < buf.len; ++i)
            arr[i] = buf.data[i];
        return arr;
    }();

    constexpr static std::string_view value{storage.data(), storage.size()};
};

/// True if field I of struct T has an explicit schema::rename attribute.
template <typename T, std::size_t I>
consteval bool field_has_explicit_rename() {
    using field_t = refl::field_type<T, I>;
    if constexpr(!serde::has_attrs<field_t>) {
        return false;
    } else {
        return serde::detail::tuple_any_of_v<typename field_t::attrs, serde::is_rename_attr>;
    }
}

/// Resolve the wire name for field I of struct T under Config.
/// Priority:
///   1. Explicit rename<"name"> on the field -> use canonical_field_name (already resolved).
///   2. Config has field_rename and it's not identity -> apply rename_all via wire_name_static.
///   3. Otherwise -> use canonical_field_name (= reflection name).
template <typename T, std::size_t I, typename Config>
consteval std::string_view resolve_wire_name() {
    if constexpr(field_has_explicit_rename<T, I>()) {
        // Explicit per-field rename takes precedence over rename_all.
        return schema::canonical_field_name<T, I>();
    } else if constexpr(requires { typename Config::field_rename; }) {
        using policy = typename Config::field_rename;
        if constexpr(std::is_same_v<policy, serde::spelling::rename_policy::identity>) {
            return schema::canonical_field_name<T, I>();
        } else {
            return wire_name_static<T, I, policy>::value;
        }
    } else {
        return schema::canonical_field_name<T, I>();
    }
}

// ===================================================================
// Behavior attribute helpers
// ===================================================================

/// Filter a tuple of attrs down to only behavior attributes.
template <typename Tuple>
struct filter_behavior_attrs;

template <>
struct filter_behavior_attrs<std::tuple<>> {
    using type = std::tuple<>;
};

template <typename First, typename... Rest>
struct filter_behavior_attrs<std::tuple<First, Rest...>> {
    using tail = typename filter_behavior_attrs<std::tuple<Rest...>>::type;
    using type =
        std::conditional_t<serde::is_behavior_attr_v<First> || serde::is_tagged_attr<First>::value,
                           decltype(std::tuple_cat(std::declval<std::tuple<First>>(),
                                                   std::declval<tail>())),
                           tail>;
};

template <typename Tuple>
using filter_behavior_attrs_t = typename filter_behavior_attrs<Tuple>::type;

/// Detect whether a type T declares a nested `wire_type` alias.
template <typename T, typename = void>
struct adapter_has_wire_type : std::false_type {};

template <typename T>
struct adapter_has_wire_type<T, std::void_t<typename T::wire_type>> : std::true_type {};

/// True if AttrsTuple contains a with<Adapter> whose Adapter declares wire_type.
template <typename AttrsTuple>
constexpr bool has_with_wire_type_v = [] {
    if constexpr(!serde::detail::tuple_has_spec_v<AttrsTuple, serde::behavior::with>) {
        return false;
    } else {
        using with_attr = serde::detail::tuple_find_spec_t<AttrsTuple, serde::behavior::with>;
        using adapter_t = typename with_attr::adapter;
        return adapter_has_wire_type<adapter_t>::value;
    }
}();

/// Extract Adapter::wire_type from a with<Adapter> in AttrsTuple.
/// Only instantiated when has_with_wire_type_v is true.
template <typename AttrsTuple>
struct extract_with_wire_type {
    using with_attr = serde::detail::tuple_find_spec_t<AttrsTuple, serde::behavior::with>;
    using type = typename with_attr::adapter::wire_type;
};

/// Determine the wire type for a field given its raw type and attrs tuple.
/// - as<Target>                           => Target
/// - enum_string<...>                     => std::string_view
/// - with<Adapter> where Adapter has wire_type => Adapter::wire_type
/// - otherwise                            => RawType
///
/// Uses partial specialization to avoid eager evaluation of nested aliases.
template <typename RawType,
          typename AttrsTuple,
          bool HasAs = serde::detail::tuple_has_spec_v<AttrsTuple, serde::behavior::as>,
          bool HasEnumStr =
              serde::detail::tuple_has_spec_v<AttrsTuple, serde::behavior::enum_string>,
          bool HasWithWireType = has_with_wire_type_v<AttrsTuple>>
struct resolve_wire_type {
    using type = RawType;
};

template <typename RawType, typename AttrsTuple, bool HasEnumStr, bool HasWithWireType>
struct resolve_wire_type<RawType, AttrsTuple, /*HasAs=*/true, HasEnumStr, HasWithWireType> {
    using type = typename serde::detail::tuple_find_spec_t<AttrsTuple, serde::behavior::as>::target;
};

template <typename RawType, typename AttrsTuple, bool HasWithWireType>
struct resolve_wire_type<RawType,
                         AttrsTuple,
                         /*HasAs=*/false,
                         /*HasEnumStr=*/true,
                         HasWithWireType> {
    using type = std::string_view;
};

template <typename RawType, typename AttrsTuple>
struct resolve_wire_type<RawType,
                         AttrsTuple,
                         /*HasAs=*/false,
                         /*HasEnumStr=*/false,
                         /*HasWithWireType=*/true> {
    using type = typename extract_with_wire_type<AttrsTuple>::type;
};

template <typename RawType, typename AttrsTuple>
using resolve_wire_type_t = typename resolve_wire_type<RawType, AttrsTuple>::type;

/// Unwrap annotated_type to get the underlying raw type.
template <typename T>
struct unwrap_annotated {
    using raw_type = T;
    using attrs = std::tuple<>;
};

template <serde::annotated_type T>
struct unwrap_annotated<T> {
    using raw_type = typename std::remove_cvref_t<T>::annotated_type;
    using attrs = typename std::remove_cvref_t<T>::attrs;
};

// ===================================================================
// Alias storage -- each field with aliases gets a static array
// ===================================================================

template <typename T, std::size_t I>
struct alias_storage {
    constexpr static bool has_alias = [] {
        using field_t = refl::field_type<T, I>;
        if constexpr(!serde::has_attrs<field_t>) {
            return false;
        } else {
            return serde::detail::tuple_any_of_v<typename field_t::attrs, serde::is_alias_attr>;
        }
    }();

    constexpr static std::size_t count = [] {
        using field_t = refl::field_type<T, I>;
        if constexpr(!serde::has_attrs<field_t>) {
            return std::size_t{0};
        } else if constexpr(!serde::detail::tuple_any_of_v<typename field_t::attrs,
                                                           serde::is_alias_attr>) {
            return std::size_t{0};
        } else {
            using alias_attr =
                serde::detail::tuple_find_t<typename field_t::attrs, serde::is_alias_attr>;
            return alias_attr::names.size();
        }
    }();

    constexpr static auto names = [] {
        using field_t = refl::field_type<T, I>;
        if constexpr(!serde::has_attrs<field_t>) {
            return std::array<std::string_view, 0>{};
        } else if constexpr(!serde::detail::tuple_any_of_v<typename field_t::attrs,
                                                           serde::is_alias_attr>) {
            return std::array<std::string_view, 0>{};
        } else {
            using alias_attr =
                serde::detail::tuple_find_t<typename field_t::attrs, serde::is_alias_attr>;
            return alias_attr::names;
        }
    }();
};

// ===================================================================
// Effective field count (accounting for skip / flatten)
// ===================================================================

/// Count contribution of a single physical field.
template <typename T, std::size_t I>
consteval std::size_t single_field_count() {
    if constexpr(schema::is_field_skipped<T, I>()) {
        return 0;
    } else if constexpr(schema::is_field_flattened<T, I>()) {
        // Flatten: the field's underlying type must be reflectable.
        // Forward-declare the recursive call by using a lambda to defer lookup.
        using field_t = refl::field_type<T, I>;
        using inner_t = typename unwrap_annotated<field_t>::raw_type;
        static_assert(refl::reflectable_class<std::remove_cvref_t<inner_t>>,
                      "flatten requires the field type to be a reflectable struct");
        // Recurse into the inner struct. effective_field_count is defined below
        // but visible here because both are in the same namespace.
        constexpr std::size_t N_inner = refl::field_count<std::remove_cvref_t<inner_t>>();
        if constexpr(N_inner == 0) {
            return 0;
        } else {
            return []<std::size_t... Js>(std::index_sequence<Js...>) consteval {
                return (single_field_count<std::remove_cvref_t<inner_t>, Js>() + ...);
            }(std::make_index_sequence<N_inner>{});
        }
    } else {
        return 1;
    }
}

/// Total number of logical fields for a reflectable struct (skip, flatten expanded).
template <typename T>
    requires refl::reflectable_class<std::remove_cvref_t<T>>
consteval std::size_t effective_field_count() {
    using V = std::remove_cvref_t<T>;
    constexpr std::size_t N = refl::field_count<V>();
    if constexpr(N == 0) {
        return 0;
    } else {
        return []<std::size_t... Is>(std::index_sequence<Is...>) consteval {
            return (single_field_count<V, Is>() + ...);
        }(std::make_index_sequence<N>{});
    }
}

// ===================================================================
// Single-field contribution for slots (type_list)
// ===================================================================

/// Primary template: normal field -> type_list with one field_slot.
template <typename T,
          typename Config,
          std::size_t I,
          bool Skipped = schema::is_field_skipped<T, I>(),
          bool Flattened = schema::is_field_flattened<T, I>()>
struct single_field_slots {
    using field_t = refl::field_type<T, I>;
    using unwrap = unwrap_annotated<field_t>;
    using raw_type = typename unwrap::raw_type;
    using attrs_t = typename unwrap::attrs;

    using type = type_list<field_slot<raw_type,
                                      resolve_wire_type_t<raw_type, attrs_t>,
                                      filter_behavior_attrs_t<attrs_t>>>;
};

/// Skipped field -> empty type_list.
template <typename T, typename Config, std::size_t I, bool Flattened>
struct single_field_slots<T, Config, I, /*Skipped=*/true, Flattened> {
    using type = type_list<>;
};

/// Flattened field -> delegate to inner struct's virtual_schema slots.
template <typename T, typename Config, std::size_t I>
struct single_field_slots<T, Config, I, /*Skipped=*/false, /*Flattened=*/true> {
    using field_t = refl::field_type<T, I>;
    using inner_t = typename unwrap_annotated<field_t>::raw_type;
    using type = typename virtual_schema<inner_t, Config>::slots;
};

// ===================================================================
// Build slots by concatenating per-field contributions
// ===================================================================

template <typename T, typename Config, typename Seq>
struct build_slots_from_seq;

template <typename T, typename Config, std::size_t... Is>
struct build_slots_from_seq<T, Config, std::index_sequence<Is...>> {
    using type = type_list_concat_t<typename single_field_slots<T, Config, Is>::type...>;
};

template <typename T, typename Config>
struct build_slots_from_seq<T, Config, std::index_sequence<>> {
    using type = type_list<>;
};

template <typename T, typename Config>
using build_slots_t =
    typename build_slots_from_seq<T, Config, std::make_index_sequence<refl::field_count<T>()>>::
        type;

// ===================================================================
// Build fields array (constexpr)
// ===================================================================

/// Forward declaration for mutual recursion with build_fields.
template <typename T, typename Config, std::size_t I>
consteval void fill_field(auto& result, std::size_t& out, std::size_t base_offset);

/// Build a single field_info for physical field I of struct T.
template <typename T, typename Config, std::size_t I>
consteval field_info make_field_info(std::size_t base_offset) {
    using field_t = refl::field_type<T, I>;
    using unwrap = unwrap_annotated<field_t>;
    using raw_type = typename unwrap::raw_type;
    using attrs_t = typename unwrap::attrs;
    using wire_t = resolve_wire_type_t<raw_type, attrs_t>;

    // Determine wire name: explicit rename > Config::field_rename (rename_all) > reflection name.
    std::string_view name = resolve_wire_name<T, I, Config>();

    // Aliases -- reference the static storage so the span stays valid.
    auto& alias_arr = alias_storage<T, I>::names;
    std::span<const std::string_view> aliases{alias_arr.data(), alias_arr.size()};

    // Offset (base_offset is a parameter, not a constant expression;
    // but we are inside a consteval function so everything is compile-time).
    std::size_t offset = base_offset + refl::field_offset<T>(I);

    // Flags
    constexpr bool has_default = [] {
        if constexpr(!serde::has_attrs<field_t>) {
            return false;
        } else {
            return serde::detail::tuple_has_v<typename std::remove_cvref_t<field_t>::attrs,
                                              schema::default_value>;
        }
    }();

    constexpr bool is_literal = [] {
        if constexpr(!serde::has_attrs<field_t>) {
            return false;
        } else {
            return serde::detail::tuple_any_of_v<typename std::remove_cvref_t<field_t>::attrs,
                                                 serde::is_literal_attr>;
        }
    }();

    constexpr bool has_skip_if = [] {
        if constexpr(!serde::has_attrs<field_t>) {
            return false;
        } else {
            return serde::detail::tuple_has_spec_v<typename std::remove_cvref_t<field_t>::attrs,
                                                   serde::behavior::skip_if>;
        }
    }();

    constexpr bool has_behavior = [] {
        if constexpr(!serde::has_attrs<field_t>) {
            return false;
        } else {
            return serde::detail::tuple_any_of_v<typename std::remove_cvref_t<field_t>::attrs,
                                                 serde::is_behavior_provider>;
        }
    }();

    return field_info{
        .name = name,
        .aliases = aliases,
        .offset = offset,
        .physical_index = I,
        .type = type_info_of<wire_t, Config>(),
        .has_default = has_default,
        .is_literal = is_literal,
        .has_skip_if = has_skip_if,
        .has_behavior = has_behavior,
    };
}

/// Recursively collect field_info entries into a fixed-size array.
/// For a normal field: 1 entry. For flatten: recursively inline the inner
/// struct's fields with adjusted offset. For skip: 0 entries.
///
/// We build via an output-index approach: write into a pre-sized array
/// at successive positions.
template <typename T, typename Config>
consteval auto build_fields(std::size_t base_offset = 0) {
    constexpr std::size_t count = effective_field_count<T>();
    std::array<field_info, count> result{};
    std::size_t out = 0;

    constexpr std::size_t N = refl::field_count<T>();
    if constexpr(N > 0) {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) consteval {
            (fill_field<T, Config, Is>(result, out, base_offset), ...);
        }(std::make_index_sequence<N>{});
    }

    return result;
}

template <typename T, typename Config, std::size_t I>
consteval void fill_field(auto& result, std::size_t& out, std::size_t base_offset) {
    if constexpr(schema::is_field_skipped<T, I>()) {
        // skip: do nothing
    } else if constexpr(schema::is_field_flattened<T, I>()) {
        using field_t = refl::field_type<T, I>;
        using inner_t = typename unwrap_annotated<field_t>::raw_type;
        std::size_t inner_offset = base_offset + refl::field_offset<T>(I);
        auto inner = build_fields<inner_t, Config>(inner_offset);
        for(std::size_t i = 0; i < inner.size(); ++i) {
            result[out++] = inner[i];
        }
    } else {
        result[out++] = make_field_info<T, Config, I>(base_offset);
    }
}

// ===================================================================
// Struct-level deny_unknown_fields detection
// ===================================================================

template <typename T>
consteval bool has_deny_unknown_fields() {
    // deny_unknown_fields is a struct-level attribute. It can appear as an
    // attribute on the struct type itself when the struct is wrapped with
    // annotation<MyStruct, deny_unknown_fields>.  However, the more common
    // pattern is to check the *first field* or a struct-level annotation list.
    // For now we check if T itself is annotated with deny_unknown_fields.
    if constexpr(serde::has_attrs<T>) {
        return serde::detail::tuple_has_v<typename T::attrs, schema::deny_unknown_fields>;
    } else {
        return false;
    }
}

// ===================================================================
// enum_values_as_i64 — convert enum member values to int64 for runtime access
// ===================================================================

template <typename E>
    requires std::is_enum_v<E>
struct enum_values_as_i64 {
    constexpr static auto values = [] {
        constexpr auto& src = refl::reflection<E>::member_values;
        std::array<std::int64_t, src.size()> out{};
        for(std::size_t i = 0; i < src.size(); ++i) {
            out[i] = static_cast<std::int64_t>(static_cast<std::underlying_type_t<E>>(src[i]));
        }
        return out;
    }();
};

/// Declaration only — address known, value defined after virtual_schema.
/// Breaks self-referential constexpr cycles via declaration/definition separation.
template <typename V, typename Config>
struct struct_info_node {
    const static struct_type_info value;
};

}  // namespace detail

// ===================================================================
// type_info_of -- static storage for type descriptors
// ===================================================================

/// Returns a pointer to a static type descriptor for T.
/// Uses if-constexpr dispatch with local static constexpr storage.
template <typename T, typename Config>
constexpr const type_info* type_info_of() {
    using V = std::remove_cvref_t<T>;

    if constexpr(schema_opaque<V>) {
        constexpr static type_info info = {type_kind::unknown, refl::type_name<T>()};
        return &info;
    } else if constexpr(is_optional_v<V>) {
        using inner_t = typename V::value_type;
        constexpr static optional_type_info info = {
            {type_kind::optional, refl::type_name<T>()},
            type_info_of<inner_t, Config>(),
        };
        return &info;
    } else if constexpr(is_specialization_of<std::unique_ptr, V>) {
        using inner_t = typename V::element_type;
        constexpr static optional_type_info info = {
            {type_kind::pointer, refl::type_name<T>()},
            type_info_of<inner_t, Config>(),
        };
        return &info;
    } else if constexpr(is_specialization_of<std::shared_ptr, V>) {
        using inner_t = typename V::element_type;
        constexpr static optional_type_info info = {
            {type_kind::pointer, refl::type_name<T>()},
            type_info_of<inner_t, Config>(),
        };
        return &info;
    } else if constexpr(is_specialization_of<std::variant, V>) {
        return []<typename... Ts>(std::type_identity<std::variant<Ts...>>) {
            constexpr static std::array<const type_info*, sizeof...(Ts)> alts = {
                type_info_of<Ts, Config>()...};
            constexpr static variant_type_info info = {
                {type_kind::variant, refl::type_name<V>()},
                {alts.data(), alts.size()},
                tag_mode::none,
                {},
                {},
                {},
            };
            return &info;
        }(std::type_identity<V>{});
    } else if constexpr(is_specialization_of<std::pair, V>) {
        constexpr static std::array<const type_info*, 2> elems = {
            type_info_of<typename V::first_type, Config>(),
            type_info_of<typename V::second_type, Config>(),
        };
        constexpr static tuple_type_info info = {
            {type_kind::tuple, refl::type_name<T>()},
            {elems.data(),     elems.size()        },
        };
        return &info;
    } else if constexpr(is_specialization_of<std::tuple, V>) {
        return []<typename... Ts>(std::type_identity<std::tuple<Ts...>>) {
            constexpr static std::array<const type_info*, sizeof...(Ts)> elems = {
                type_info_of<Ts, Config>()...};
            constexpr static tuple_type_info info = {
                {type_kind::tuple, refl::type_name<V>()},
                {elems.data(),     elems.size()        },
            };
            return &info;
        }(std::type_identity<V>{});
    } else if constexpr(std::ranges::input_range<V> && !serde::str_like<V> &&
                        !serde::bytes_like<V>) {
        constexpr auto fmt = format_kind<V>;
        if constexpr(fmt == range_format::map) {
            using kv_t = std::ranges::range_value_t<V>;
            using key_t = std::remove_const_t<typename kv_t::first_type>;
            using mapped_t = typename kv_t::second_type;
            constexpr static map_type_info info = {
                {type_kind::map, refl::type_name<T>()},
                type_info_of<key_t, Config>(),
                type_info_of<mapped_t, Config>(),
            };
            return &info;
        } else if constexpr(fmt == range_format::set) {
            using element_t = std::ranges::range_value_t<V>;
            constexpr static array_type_info info = {
                {type_kind::set, refl::type_name<T>()},
                type_info_of<element_t, Config>(),
            };
            return &info;
        } else {
            using element_t = std::ranges::range_value_t<V>;
            constexpr static array_type_info info = {
                {type_kind::array, refl::type_name<T>()},
                type_info_of<element_t, Config>(),
            };
            return &info;
        }
    } else if constexpr(refl::reflectable_class<V>) {
        return &detail::struct_info_node<V, Config>::value;
    } else if constexpr(std::is_enum_v<V>) {
        constexpr static auto& names = refl::reflection<V>::member_names;
        constexpr static auto& values = detail::enum_values_as_i64<V>::values;
        constexpr static enum_type_info info = {
            {type_kind::enumeration, refl::type_name<T>()},
            {names.data(),           names.size()        },
            {values.data(),          values.size()       },
            kind_of<std::underlying_type_t<V>>(),
        };
        return &info;
    } else {
        constexpr static type_info info = {kind_of<T>(), refl::type_name<T>()};
        return &info;
    }
}

// ===================================================================
// virtual_schema -- the core schema descriptor for reflectable structs
// ===================================================================

template <typename T, typename Config>
    requires refl::reflectable_class<std::remove_cvref_t<T>>
struct virtual_schema {
    using V = std::remove_cvref_t<T>;

    /// Number of logical (non-skipped, flatten-expanded) fields.
    constexpr static std::size_t count = detail::effective_field_count<V>();

    /// Compile-time field descriptors.
    constexpr static std::array<field_info, count> fields = detail::build_fields<V, Config>();

    /// Per-field type-level slots for compile-time dispatch.
    using slots = detail::build_slots_t<V, Config>;

    /// Whether the struct is trivially copyable (POD-like, candidate for inline/fixed-size layout).
    constexpr static bool is_trivially_copyable =
        std::is_trivial_v<V> && std::is_standard_layout_v<V>;

    /// Whether unknown fields should be rejected during deserialization.
    constexpr static bool deny_unknown = detail::has_deny_unknown_fields<V>();
};

namespace detail {

template <typename V, typename Config>
const struct_type_info struct_info_node<V, Config>::value = {
    {type_kind::structure, refl::type_name<V>()},
    {virtual_schema<V, Config>::fields.data(), virtual_schema<V, Config>::count},
    virtual_schema<V, Config>::is_trivially_copyable,
};

}  // namespace detail

}  // namespace eventide::serde::schema
