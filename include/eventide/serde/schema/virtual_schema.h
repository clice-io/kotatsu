#pragma once

#include <array>
#include <cstddef>
#include <cstdlib>
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

template <typename T, typename Config = serde::config::default_config>
constexpr const type_info* type_info_of();

template <typename T, typename Config = serde::config::default_config>
    requires refl::reflectable_class<T>
struct virtual_schema;

namespace detail {

struct name_buf {
    std::array<char, 64> data{};
    std::size_t len = 0;

    constexpr void push(char c) {
        if(len >= data.size()) {
            std::abort();
        }
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

constexpr name_buf snake_to_upper_cx(std::string_view text) {
    auto snake = normalize_to_lower_snake_cx(text);
    for(std::size_t i = 0; i < snake.len; ++i) {
        snake.data[i] = cx_upper(snake.data[i]);
    }
    return snake;
}

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

template <typename T, std::size_t I>
consteval bool field_has_explicit_rename() {
    using field_t = refl::field_type<T, I>;
    if constexpr(!serde::has_attrs<field_t>) {
        return false;
    } else {
        return serde::detail::tuple_any_of_v<typename field_t::attrs, serde::is_rename_attr>;
    }
}

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

template <typename T>
constexpr bool has_wire_type_v = requires { typename T::wire_type; };

template <typename AttrsTuple>
constexpr bool has_with_wire_type_v = [] {
    if constexpr(!serde::detail::tuple_has_spec_v<AttrsTuple, serde::behavior::with>) {
        return false;
    } else {
        using with_attr = serde::detail::tuple_find_spec_t<AttrsTuple, serde::behavior::with>;
        return has_wire_type_v<typename with_attr::adapter>;
    }
}();

template <typename AttrsTuple>
struct extract_with_wire_type {
    using with_attr = serde::detail::tuple_find_spec_t<AttrsTuple, serde::behavior::with>;
    using type = typename with_attr::adapter::wire_type;
};

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

template <typename BaseConfig,
          typename AttrsTuple,
          bool HasRenameAll = serde::detail::tuple_has_spec_v<AttrsTuple, schema::rename_all>>
struct struct_schema_config {
    using type = BaseConfig;
};

template <typename BaseConfig, typename AttrsTuple>
struct struct_schema_config<BaseConfig, AttrsTuple, true> {
    struct type : BaseConfig {
        using field_rename =
            typename serde::detail::tuple_find_spec_t<AttrsTuple, schema::rename_all>::policy;
    };
};

template <typename BaseConfig, typename AttrsTuple>
using struct_schema_config_t = typename struct_schema_config<BaseConfig, AttrsTuple>::type;

template <typename AttrsTuple>
constexpr bool has_struct_schema_attrs_v =
    serde::detail::tuple_has_spec_v<AttrsTuple, schema::rename_all> ||
    serde::detail::tuple_has_v<AttrsTuple, schema::deny_unknown_fields>;

template <typename AttrsTuple>
constexpr bool has_tagged_schema_attr_v =
    serde::detail::tuple_any_of_v<AttrsTuple, serde::is_tagged_attr>;

template <typename T, std::size_t I>
consteval bool has_alias_attr() {
    using field_t = refl::field_type<T, I>;
    using attrs_t = typename unwrap_annotated<field_t>::attrs;
    return serde::detail::tuple_any_of_v<attrs_t, serde::is_alias_attr>;
}

template <typename T, std::size_t I, bool HasAlias = has_alias_attr<T, I>()>
struct alias_storage {
    constexpr static bool has_alias = false;
    constexpr static std::size_t count = 0;
    constexpr static std::array<std::string_view, 0> names = {};
};

template <typename T, std::size_t I>
struct alias_storage<T, I, true> {
    constexpr static bool has_alias = true;

    using field_t = refl::field_type<T, I>;
    using attrs_t = typename unwrap_annotated<field_t>::attrs;
    using alias_attr = serde::detail::tuple_find_t<attrs_t, serde::is_alias_attr>;

    constexpr static std::size_t count = alias_attr::names.size();
    constexpr static auto names = alias_attr::names;
};

template <typename T, std::size_t I>
consteval std::size_t single_field_count() {
    using field_t = refl::field_type<T, I>;
    using attrs_t = typename unwrap_annotated<field_t>::attrs;
    constexpr bool skipped = serde::detail::tuple_has_v<attrs_t, schema::skip>;
    constexpr bool flattened = serde::detail::tuple_has_v<attrs_t, schema::flatten>;

    if constexpr(skipped) {
        return 0;
    } else if constexpr(flattened) {
        using inner_t = typename unwrap_annotated<field_t>::raw_type;
        static_assert(refl::reflectable_class<std::remove_cvref_t<inner_t>>,
                      "flatten requires the field type to be a reflectable struct");
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

template <typename T>
    requires refl::reflectable_class<T>
consteval std::size_t effective_field_count() {
    constexpr std::size_t N = refl::field_count<T>();
    if constexpr(N == 0) {
        return 0;
    } else {
        return []<std::size_t... Is>(std::index_sequence<Is...>) consteval {
            return (single_field_count<T, Is>() + ...);
        }(std::make_index_sequence<N>{});
    }
}

template <typename T, std::size_t I>
struct field_attr_flags {
    using field_t = refl::field_type<T, I>;
    using attrs_t = typename unwrap_annotated<field_t>::attrs;
    constexpr static bool skipped = serde::detail::tuple_has_v<attrs_t, schema::skip>;
    constexpr static bool flattened = serde::detail::tuple_has_v<attrs_t, schema::flatten>;
};

template <typename T,
          typename Config,
          std::size_t I,
          bool Skipped = field_attr_flags<T, I>::skipped,
          bool Flattened = field_attr_flags<T, I>::flattened>
struct single_field_slots {
    using field_t = refl::field_type<T, I>;
    using unwrap = unwrap_annotated<field_t>;
    using raw_type = typename unwrap::raw_type;
    using attrs_t = typename unwrap::attrs;

    using type = type_list<field_slot<raw_type,
                                      resolve_wire_type_t<raw_type, attrs_t>,
                                      filter_behavior_attrs_t<attrs_t>>>;
};

template <typename T, typename Config, std::size_t I, bool Flattened>
struct single_field_slots<T, Config, I, /*Skipped=*/true, Flattened> {
    using type = type_list<>;
};

template <typename T, typename Config, std::size_t I>
struct single_field_slots<T, Config, I, /*Skipped=*/false, /*Flattened=*/true> {
    using field_t = refl::field_type<T, I>;
    using inner_t = typename unwrap_annotated<field_t>::raw_type;
    using type = typename virtual_schema<inner_t, Config>::slots;
};

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

template <typename T, typename Config, std::size_t I>
consteval void fill_field(auto& result, std::size_t& out, std::size_t base_offset);

template <typename T, typename Config, std::size_t I>
consteval field_info make_field_info(std::size_t base_offset) {
    using field_t = refl::field_type<T, I>;
    using attrs_t = typename unwrap_annotated<field_t>::attrs;

    std::string_view name = resolve_wire_name<T, I, Config>();

    auto& alias_arr = alias_storage<T, I>::names;
    std::span<const std::string_view> aliases{alias_arr.data(), alias_arr.size()};

    std::size_t offset = base_offset + refl::field_offset<T>(I);
    constexpr bool has_default = serde::detail::tuple_has_v<attrs_t, schema::default_value>;
    constexpr bool is_literal = serde::detail::tuple_any_of_v<attrs_t, serde::is_literal_attr>;
    constexpr bool has_skip_if = serde::detail::tuple_has_spec_v<attrs_t, serde::behavior::skip_if>;
    constexpr bool has_behavior =
        serde::detail::tuple_any_of_v<attrs_t, serde::is_behavior_provider>;

    return field_info{
        .name = name,
        .aliases = aliases,
        .offset = offset,
        .physical_index = I,
        .type = type_info_of<field_t, Config>(),
        .has_default = has_default,
        .is_literal = is_literal,
        .has_skip_if = has_skip_if,
        .has_behavior = has_behavior,
    };
}

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
    using field_t_ = refl::field_type<T, I>;
    using attrs_t_ = typename unwrap_annotated<field_t_>::attrs;
    constexpr bool skipped = serde::detail::tuple_has_v<attrs_t_, schema::skip>;
    constexpr bool flattened = serde::detail::tuple_has_v<attrs_t_, schema::flatten>;

    if constexpr(skipped) {
        // skip: do nothing
    } else if constexpr(flattened) {
        using inner_t = typename unwrap_annotated<field_t_>::raw_type;
        std::size_t inner_offset = base_offset + refl::field_offset<T>(I);
        auto inner = build_fields<inner_t, Config>(inner_offset);
        for(std::size_t i = 0; i < inner.size(); ++i) {
            result[out++] = inner[i];
        }
    } else {
        result[out++] = make_field_info<T, Config, I>(base_offset);
    }
}

template <typename T>
consteval bool has_deny_unknown_fields() {
    using attrs_t = typename unwrap_annotated<T>::attrs;
    return serde::detail::tuple_has_v<attrs_t, schema::deny_unknown_fields>;
}

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

template <typename E>
    requires std::is_enum_v<E>
struct enum_values_as_u64 {
    constexpr static auto values = [] {
        constexpr auto& src = refl::reflection<E>::member_values;
        std::array<std::uint64_t, src.size()> out{};
        for(std::size_t i = 0; i < src.size(); ++i) {
            out[i] = static_cast<std::uint64_t>(static_cast<std::underlying_type_t<E>>(src[i]));
        }
        return out;
    }();
};

/// Forward declaration — full definition after virtual_schema.
template <typename V, typename Config>
struct struct_info_node;

template <typename V, typename Config, typename AttrsTuple>
struct annotated_struct_info_node {
    using schema_config = struct_schema_config_t<Config, AttrsTuple>;

    constexpr static std::size_t count = effective_field_count<V>();
    constexpr static std::array<field_info, count> fields = build_fields<V, schema_config>();
    constexpr static bool is_trivially_copyable =
        std::is_trivial_v<V> && std::is_standard_layout_v<V>;
    constexpr static bool deny_unknown =
        serde::detail::tuple_has_v<AttrsTuple, schema::deny_unknown_fields>;

    const inline static struct_type_info value = {
        {type_kind::structure, refl::type_name<V>()},
        {fields.data(),        count               },
        is_trivially_copyable,
        deny_unknown,
    };
};

template <typename TagAttr>
consteval tag_mode tagged_mode_for() {
    constexpr auto strategy = serde::tagged_strategy_of<TagAttr>;
    if constexpr(strategy == serde::tagged_strategy::external) {
        return tag_mode::external;
    } else if constexpr(strategy == serde::tagged_strategy::internal) {
        return tag_mode::internal;
    } else {
        return tag_mode::adjacent;
    }
}

template <typename Variant, typename Config, typename AttrsTuple>
struct annotated_variant_info_node;

template <typename Config, typename AttrsTuple, typename... Ts>
struct annotated_variant_info_node<std::variant<Ts...>, Config, AttrsTuple> {
    using variant_t = std::variant<Ts...>;
    using tag_attr = serde::detail::tuple_find_t<AttrsTuple, serde::is_tagged_attr>;

    constexpr static auto alt_names = serde::resolve_tag_names<tag_attr, Ts...>();
    constexpr static std::array<const type_info*, sizeof...(Ts)> alternatives = {
        type_info_of<Ts, Config>()...};
    constexpr static tag_mode tagging = tagged_mode_for<tag_attr>();
    constexpr static std::string_view tag_field =
        tagging == tag_mode::external ? std::string_view{} : tag_attr::field_names[0];
    constexpr static std::string_view content_field =
        tagging == tag_mode::adjacent ? tag_attr::field_names[1] : std::string_view{};

    const inline static variant_type_info value = {
        {type_kind::variant,  refl::type_name<variant_t>()},
        {alternatives.data(), alternatives.size()         },
        tagging,
        tag_field,
        content_field,
        {alt_names.data(),    alt_names.size()            },
    };
};

template <typename T, typename Config>
constexpr const type_info* type_info_of_annotated() {
    using unwrap = unwrap_annotated<T>;
    using raw_t = typename unwrap::raw_type;
    using attrs_t = typename unwrap::attrs;
    using wire_t = resolve_wire_type_t<raw_t, attrs_t>;

    if constexpr(has_tagged_schema_attr_v<attrs_t> && is_specialization_of<std::variant, wire_t>) {
        return &annotated_variant_info_node<wire_t, Config, attrs_t>::value;
    } else if constexpr(refl::reflectable_class<wire_t> && has_struct_schema_attrs_v<attrs_t>) {
        return &annotated_struct_info_node<wire_t, Config, attrs_t>::value;
    } else {
        return type_info_of<wire_t, Config>();
    }
}

}  // namespace detail

/// Returns a pointer to a static type descriptor for T.
template <typename T, typename Config>
constexpr const type_info* type_info_of() {
    // Strip cv-qualifiers: T may carry const from refl::field_type.
    if constexpr(!std::is_same_v<T, std::remove_cv_t<T>>) {
        return type_info_of<std::remove_cv_t<T>, Config>();
    } else if constexpr(serde::annotated_type<T>) {
        return detail::type_info_of_annotated<T, Config>();
    } else if constexpr(schema_opaque<T>) {
        constexpr static type_info info = {type_kind::unknown, refl::type_name<T>()};
        return &info;
    } else if constexpr(is_optional_v<T>) {
        using inner_t = typename T::value_type;
        constexpr static optional_type_info info = {
            {type_kind::optional, refl::type_name<T>()},
            type_info_of<inner_t, Config>(),
        };
        return &info;
    } else if constexpr(is_specialization_of<std::unique_ptr, T>) {
        using inner_t = typename T::element_type;
        constexpr static optional_type_info info = {
            {type_kind::pointer, refl::type_name<T>()},
            type_info_of<inner_t, Config>(),
        };
        return &info;
    } else if constexpr(is_specialization_of<std::shared_ptr, T>) {
        using inner_t = typename T::element_type;
        constexpr static optional_type_info info = {
            {type_kind::pointer, refl::type_name<T>()},
            type_info_of<inner_t, Config>(),
        };
        return &info;
    } else if constexpr(is_specialization_of<std::variant, T>) {
        return []<typename... Ts>(std::type_identity<std::variant<Ts...>>) {
            constexpr static std::array<const type_info*, sizeof...(Ts)> alts = {
                type_info_of<Ts, Config>()...};
            constexpr static variant_type_info info = {
                {type_kind::variant, refl::type_name<T>()},
                {alts.data(), alts.size()},
                tag_mode::none,
                {},
                {},
                {},
            };
            return &info;
        }(std::type_identity<T>{});
    } else if constexpr(is_specialization_of<std::pair, T>) {
        constexpr static std::array<const type_info*, 2> elems = {
            type_info_of<typename T::first_type, Config>(),
            type_info_of<typename T::second_type, Config>(),
        };
        constexpr static tuple_type_info info = {
            {type_kind::tuple, refl::type_name<T>()},
            {elems.data(),     elems.size()        },
        };
        return &info;
    } else if constexpr(is_specialization_of<std::tuple, T>) {
        return []<typename... Ts>(std::type_identity<std::tuple<Ts...>>) {
            constexpr static std::array<const type_info*, sizeof...(Ts)> elems = {
                type_info_of<Ts, Config>()...};
            constexpr static tuple_type_info info = {
                {type_kind::tuple, refl::type_name<T>()},
                {elems.data(),     elems.size()        },
            };
            return &info;
        }(std::type_identity<T>{});
    } else if constexpr(std::ranges::input_range<T> && !serde::str_like<T> &&
                        !serde::bytes_like<T>) {
        constexpr auto fmt = format_kind<T>;
        if constexpr(fmt == range_format::map) {
            using kv_t = std::ranges::range_value_t<T>;
            using key_t = std::remove_const_t<typename kv_t::first_type>;
            using mapped_t = typename kv_t::second_type;
            constexpr static map_type_info info = {
                {type_kind::map, refl::type_name<T>()},
                type_info_of<key_t, Config>(),
                type_info_of<mapped_t, Config>(),
            };
            return &info;
        } else if constexpr(fmt == range_format::set) {
            using element_t = std::ranges::range_value_t<T>;
            constexpr static array_type_info info = {
                {type_kind::set, refl::type_name<T>()},
                type_info_of<element_t, Config>(),
            };
            return &info;
        } else if constexpr(fmt == range_format::sequence) {
            using element_t = std::ranges::range_value_t<T>;
            constexpr static array_type_info info = {
                {type_kind::array, refl::type_name<T>()},
                type_info_of<element_t, Config>(),
            };
            return &info;
        } else {
            constexpr static type_info info = {type_kind::unknown, refl::type_name<T>()};
            return &info;
        }
    } else if constexpr(refl::reflectable_class<T>) {
        return &detail::struct_info_node<T, Config>::value;
    } else if constexpr(std::is_enum_v<T>) {
        constexpr static auto& names = refl::reflection<T>::member_names;
        using underlying_t = std::underlying_type_t<T>;
        if constexpr(std::is_unsigned_v<underlying_t> && sizeof(underlying_t) == 8) {
            constexpr static auto& values = detail::enum_values_as_u64<T>::values;
            constexpr static enum_type_info info = {
                {type_kind::enumeration, refl::type_name<T>()},
                {names.data(),           names.size()        },
                {},
                {values.data(),          values.size()       },
                kind_of<underlying_t>(),
            };
            return &info;
        } else {
            constexpr static auto& values = detail::enum_values_as_i64<T>::values;
            constexpr static enum_type_info info = {
                {type_kind::enumeration, refl::type_name<T>()},
                {names.data(),           names.size()        },
                {values.data(),          values.size()       },
                {},
                kind_of<underlying_t>(),
            };
            return &info;
        }
    } else {
        constexpr static type_info info = {kind_of<T>(), refl::type_name<T>()};
        return &info;
    }
}

template <typename T, typename Config>
    requires refl::reflectable_class<T>
struct virtual_schema {
    constexpr static std::size_t count = detail::effective_field_count<T>();
    constexpr static std::array<field_info, count> fields = detail::build_fields<T, Config>();
    using slots = detail::build_slots_t<T, Config>;
    constexpr static bool is_trivially_copyable =
        std::is_trivial_v<T> && std::is_standard_layout_v<T>;
    constexpr static bool deny_unknown = detail::has_deny_unknown_fields<T>();
};

namespace detail {

template <typename V, typename Config>
struct struct_info_node {
    const inline static struct_type_info value = {
        {type_kind::structure, refl::type_name<V>()},
        {virtual_schema<V, Config>::fields.data(), virtual_schema<V, Config>::count},
        virtual_schema<V, Config>::is_trivially_copyable,
        virtual_schema<V, Config>::deny_unknown,
    };
};

}  // namespace detail

}  // namespace eventide::serde::schema
