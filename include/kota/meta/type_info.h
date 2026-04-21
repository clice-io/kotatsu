#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>

#include "annotation.h"
#include "enum.h"
#include "struct.h"
#include "type_kind.h"
#include "kota/support/naming.h"
#include "kota/support/ranges.h"
#include "kota/support/tuple_traits.h"
#include "kota/support/type_traits.h"

namespace kota::meta {

struct default_config {};

struct field_info;

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

using type_info_fn = const type_info& (*)();

struct array_type_info : type_info {
    type_info_fn element;
};

struct map_type_info : type_info {
    type_info_fn key;
    type_info_fn value;
};

struct enum_type_info : type_info {
    std::span<const std::string_view> member_names;
    const void* member_values;
    type_kind underlying_kind;
};

struct tuple_type_info : type_info {
    std::span<const type_info_fn> elements;
};

struct variant_type_info : type_info {
    std::span<const type_info_fn> alternatives;
    tag_mode tagging = tag_mode::none;
    std::string_view tag_field;
    std::string_view content_field;
    std::span<const std::string_view> alt_names;
};

struct optional_type_info : type_info {
    type_info_fn inner;
};

struct field_info {
    std::string_view name;
    std::span<const std::string_view> aliases;
    std::size_t offset;
    std::size_t physical_index;
    type_info_fn type;

    bool has_default;
    bool is_literal;
    bool has_skip_if;
    bool has_behavior;
};

struct struct_type_info : type_info {
    bool deny_unknown;
    bool is_trivial_layout;
    std::span<const field_info> fields;
};

template <typename T, typename Config = default_config>
constexpr const type_info& type_info_of();

namespace detail {

template <typename Policy>
constexpr std::string apply_rename_cx(std::string_view input) {
    using namespace naming::rename_policy;
    if constexpr(std::is_same_v<Policy, identity>) {
        return std::string(input);
    } else if constexpr(std::is_same_v<Policy, lower_snake>) {
        return naming::normalize_to_lower_snake(input);
    } else if constexpr(std::is_same_v<Policy, lower_camel>) {
        return naming::snake_to_camel(input, false);
    } else if constexpr(std::is_same_v<Policy, upper_camel>) {
        return naming::snake_to_camel(input, true);
    } else if constexpr(std::is_same_v<Policy, upper_snake>) {
        return naming::snake_to_upper(input);
    } else {
        static_assert(sizeof(Policy) == 0, "Unknown rename policy");
    }
}

template <typename T, std::size_t I, typename Policy>
struct wire_name_static {
    constexpr static std::size_t len = apply_rename_cx<Policy>(meta::field_name<I, T>()).size();

    constexpr static auto storage = [] {
        auto renamed = apply_rename_cx<Policy>(meta::field_name<I, T>());
        std::array<char, len> arr{};
        for(std::size_t i = 0; i < len; ++i)
            arr[i] = renamed[i];
        return arr;
    }();

    constexpr static std::string_view value{storage.data(), storage.size()};
};

template <typename T, std::size_t I>
constexpr bool field_has_explicit_rename() {
    using field_t = meta::field_type<T, I>;
    if constexpr(!annotated_type<field_t>) {
        return false;
    } else {
        return tuple_any_of_v<typename field_t::attrs, is_rename_attr>;
    }
}

template <typename Config>
struct effective_field_rename {
    using type = naming::rename_policy::identity;
};

template <typename Config>
    requires requires { typename Config::field_rename; }
struct effective_field_rename<Config> {
    using type = typename Config::field_rename;
};

template <typename Config>
using effective_field_rename_t = typename effective_field_rename<Config>::type;

template <typename T, std::size_t I, typename Config>
constexpr std::string_view resolve_wire_name() {
    using policy = effective_field_rename_t<Config>;
    if constexpr(field_has_explicit_rename<T, I>() ||
                 std::is_same_v<policy, naming::rename_policy::identity>) {
        return attrs::canonical_field_name<T, I>();
    } else {
        return wire_name_static<T, I, policy>::value;
    }
}

template <typename Tuple>
struct filter_runtime_attrs;

template <>
struct filter_runtime_attrs<std::tuple<>> {
    using type = std::tuple<>;
};

template <typename First, typename... Rest>
struct filter_runtime_attrs<std::tuple<First, Rest...>> {
    using tail = typename filter_runtime_attrs<std::tuple<Rest...>>::type;
    constexpr static bool keep = is_behavior_attr_v<First> || is_tagged_attr<First>::value ||
                                 std::is_same_v<First, attrs::default_value>;
    using type = std::conditional_t<keep,
                                    decltype(std::tuple_cat(std::declval<std::tuple<First>>(),
                                                            std::declval<tail>())),
                                    tail>;
};

template <typename Tuple>
using filter_runtime_attrs_t = typename filter_runtime_attrs<Tuple>::type;

template <typename T>
constexpr bool has_wire_type_v = requires { typename T::wire_type; };

template <typename AttrsTuple>
constexpr bool has_with_wire_type_v = [] {
    if constexpr(!tuple_has_spec_v<AttrsTuple, behavior::with>) {
        return false;
    } else {
        using with_attr = tuple_find_spec_t<AttrsTuple, behavior::with>;
        return has_wire_type_v<typename with_attr::adapter>;
    }
}();

template <typename AttrsTuple>
struct extract_with_wire_type {
    using with_attr = tuple_find_spec_t<AttrsTuple, behavior::with>;
    using type = typename with_attr::adapter::wire_type;
};

template <typename RawType, typename AttrsTuple>
constexpr auto resolve_wire_type_impl() {
    if constexpr(tuple_has_spec_v<AttrsTuple, behavior::as>) {
        return std::type_identity<typename tuple_find_spec_t<AttrsTuple, behavior::as>::target>{};
    } else if constexpr(tuple_has_spec_v<AttrsTuple, behavior::enum_string>) {
        return std::type_identity<std::string_view>{};
    } else if constexpr(has_with_wire_type_v<AttrsTuple>) {
        return std::type_identity<typename extract_with_wire_type<AttrsTuple>::type>{};
    } else {
        return std::type_identity<RawType>{};
    }
}

template <typename RawType, typename AttrsTuple>
using resolve_wire_type_t = typename decltype(resolve_wire_type_impl<RawType, AttrsTuple>())::type;

template <typename T>
struct unwrap_annotated {
    using raw_type = T;
    using attrs = std::tuple<>;
};

template <annotated_type T>
struct unwrap_annotated<T> {
    using raw_type = typename T::annotated_type;
    using attrs = typename T::attrs;
};

template <typename BaseConfig,
          typename AttrsTuple,
          bool HasRenameAll = tuple_has_spec_v<AttrsTuple, attrs::rename_all>>
struct struct_schema_config {
    using type = BaseConfig;
};

template <typename BaseConfig, typename AttrsTuple>
struct struct_schema_config<BaseConfig, AttrsTuple, true> {
    struct type : BaseConfig {
        using field_rename = typename tuple_find_spec_t<AttrsTuple, attrs::rename_all>::policy;
    };
};

template <typename BaseConfig, typename AttrsTuple>
using struct_schema_config_t = typename struct_schema_config<BaseConfig, AttrsTuple>::type;

template <typename AttrsTuple>
constexpr bool has_struct_schema_attrs_v = tuple_has_spec_v<AttrsTuple, attrs::rename_all> ||
                                           tuple_has_v<AttrsTuple, attrs::deny_unknown_fields>;

template <typename AttrsTuple>
using tagged_schema_attr_t = tuple_find_t<AttrsTuple, is_tagged_attr>;

template <typename AttrsTuple>
constexpr bool has_tagged_schema_attr_v = !std::is_void_v<tagged_schema_attr_t<AttrsTuple>>;

template <typename WireT, typename AttrsT, typename Config, type_kind Kind = kind_of<WireT>()>
struct type_instance_impl;

template <typename T, typename Config = default_config>
struct type_instance :
    type_instance_impl<resolve_wire_type_t<typename unwrap_annotated<std::remove_cv_t<T>>::raw_type,
                                           typename unwrap_annotated<std::remove_cv_t<T>>::attrs>,
                       typename unwrap_annotated<std::remove_cv_t<T>>::attrs,
                       Config> {};

template <typename T, std::size_t I>
constexpr bool has_alias_attr() {
    using field_t = meta::field_type<T, I>;
    using attrs_t = typename unwrap_annotated<field_t>::attrs;
    return tuple_any_of_v<attrs_t, is_alias_attr>;
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

    using field_t = meta::field_type<T, I>;
    using attrs_t = typename unwrap_annotated<field_t>::attrs;
    using alias_attr = tuple_find_t<attrs_t, is_alias_attr>;

    constexpr static std::size_t count = alias_attr::names.size();
    constexpr static auto names = alias_attr::names;
};

template <typename T, std::size_t I>
constexpr std::size_t single_field_count();

template <typename T>
    requires meta::reflectable_class<T>
constexpr std::size_t effective_field_count() {
    constexpr std::size_t N = meta::field_count<T>();
    if constexpr(N == 0) {
        return 0;
    } else {
        return []<std::size_t... Is>(std::index_sequence<Is...>) constexpr {
            return (single_field_count<T, Is>() + ...);
        }(std::make_index_sequence<N>{});
    }
}

template <typename T, std::size_t I>
constexpr std::size_t single_field_count() {
    using field_t = meta::field_type<T, I>;
    using attrs_t = typename unwrap_annotated<field_t>::attrs;
    constexpr bool skipped = tuple_has_v<attrs_t, attrs::skip>;
    constexpr bool flattened = tuple_has_v<attrs_t, attrs::flatten>;

    if constexpr(skipped) {
        return 0;
    } else if constexpr(flattened) {
        using inner_t = std::remove_cvref_t<typename unwrap_annotated<field_t>::raw_type>;
        static_assert(meta::reflectable_class<inner_t>,
                      "flatten requires the field type to be a reflectable struct");
        return effective_field_count<inner_t>();
    } else {
        return 1;
    }
}

template <typename T, std::size_t I>
struct field_attr_flags {
    using field_t = meta::field_type<T, I>;
    using attrs_t = typename unwrap_annotated<field_t>::attrs;
    constexpr static bool skipped = tuple_has_v<attrs_t, attrs::skip>;
    constexpr static bool flattened = tuple_has_v<attrs_t, attrs::flatten>;
};

template <typename T, typename Config>
using built_fields_t = std::array<field_info, effective_field_count<T>()>;

template <typename T, typename Config>
constexpr built_fields_t<T, Config> build_fields(std::size_t base_offset = 0);

template <typename T>
constexpr bool has_deny_unknown_fields() {
    using attrs_t = typename unwrap_annotated<T>::attrs;
    return tuple_has_v<attrs_t, attrs::deny_unknown_fields>;
}

template <typename TagAttr>
constexpr tag_mode tagged_mode_for() {
    constexpr auto strategy = tagged_strategy_of<TagAttr>;
    if constexpr(strategy == tagged_strategy::external) {
        return tag_mode::external;
    } else if constexpr(strategy == tagged_strategy::internal) {
        return tag_mode::internal;
    } else {
        return tag_mode::adjacent;
    }
}

template <typename Variant, typename Config, typename AttrsTuple = std::tuple<>>
struct variant_info_node;

template <typename Config, typename AttrsTuple, typename... Ts>
struct variant_info_node<std::variant<Ts...>, Config, AttrsTuple> {
    using variant_t = std::variant<Ts...>;
    constexpr static bool has_tag = has_tagged_schema_attr_v<AttrsTuple>;

    constexpr static std::array<type_info_fn, sizeof...(Ts)> alternatives = {
        type_info_of<Ts, Config>...};

    // Backing storage is always sizeof...(Ts) (>=1 since variant must have alternatives);
    // for non-tagged variants the elements stay default-constructed and the consumer
    // span below is explicitly given size 0, so no element is ever read.
    constexpr static auto alt_names = [] {
        if constexpr(has_tag) {
            return resolve_tag_names<tagged_schema_attr_t<AttrsTuple>, Ts...>();
        } else {
            return std::array<std::string_view, sizeof...(Ts)>{};
        }
    }();

    constexpr static tag_mode tagging = [] {
        if constexpr(has_tag) {
            return tagged_mode_for<tagged_schema_attr_t<AttrsTuple>>();
        } else {
            return tag_mode::none;
        }
    }();

    constexpr static std::string_view tag_field = [] {
        if constexpr(has_tag) {
            using tag_attr = tagged_schema_attr_t<AttrsTuple>;
            if constexpr(tagged_mode_for<tag_attr>() != tag_mode::external) {
                return std::string_view{tag_attr::field_names[0]};
            }
        }
        return std::string_view{};
    }();

    constexpr static std::string_view content_field = [] {
        if constexpr(has_tag) {
            using tag_attr = tagged_schema_attr_t<AttrsTuple>;
            if constexpr(tagged_mode_for<tag_attr>() == tag_mode::adjacent) {
                return std::string_view{tag_attr::field_names[1]};
            }
        }
        return std::string_view{};
    }();

    constexpr inline static variant_type_info value = {
        {type_kind::variant,  meta::type_name<variant_t>()  },
        {alternatives.data(), alternatives.size()           },
        tagging,
        tag_field,
        content_field,
        {alt_names.data(),    has_tag ? alt_names.size() : 0},
    };
};

template <typename Tuple,
          typename Config,
          typename Seq = std::make_index_sequence<std::tuple_size_v<Tuple>>>
struct tuple_info_node;

template <typename Tuple, typename Config, std::size_t... Is>
struct tuple_info_node<Tuple, Config, std::index_sequence<Is...>> {
    constexpr inline static std::array<type_info_fn, sizeof...(Is)> elements = {
        type_info_of<std::tuple_element_t<Is, Tuple>, Config>...};

    constexpr inline static tuple_type_info value = {
        {type_kind::tuple, meta::type_name<Tuple>()},
        {elements.data(),  elements.size()         },
    };
};

template <typename WireT, typename AttrsT, typename Config, type_kind Kind>
struct type_instance_impl {
    constexpr inline static type_info value = {
        kind_of<WireT>(),
        meta::type_name<WireT>(),
    };
};

template <typename WireT, typename AttrsT, typename Config>
struct type_instance_impl<WireT, AttrsT, Config, type_kind::optional> {
    using inner_t = typename WireT::value_type;

    constexpr inline static optional_type_info value = {
        {type_kind::optional, meta::type_name<WireT>()},
        type_info_of<inner_t, Config>,
    };
};

template <typename WireT, typename AttrsT, typename Config>
struct type_instance_impl<WireT, AttrsT, Config, type_kind::pointer> {
    using inner_t = typename WireT::element_type;

    constexpr inline static optional_type_info value = {
        {type_kind::pointer, meta::type_name<WireT>()},
        type_info_of<inner_t, Config>,
    };
};

template <typename WireT, typename AttrsT, typename Config>
struct type_instance_impl<WireT, AttrsT, Config, type_kind::variant> {
    constexpr inline static variant_type_info value =
        variant_info_node<WireT, Config, AttrsT>::value;
};

template <typename WireT, typename AttrsT, typename Config>
struct type_instance_impl<WireT, AttrsT, Config, type_kind::tuple> {
    constexpr inline static tuple_type_info value = tuple_info_node<WireT, Config>::value;
};

template <typename WireT, typename AttrsT, typename Config>
struct type_instance_impl<WireT, AttrsT, Config, type_kind::map> {
    using kv_t = std::ranges::range_value_t<WireT>;
    using key_t = std::remove_const_t<typename kv_t::first_type>;
    using mapped_t = typename kv_t::second_type;

    constexpr inline static map_type_info value = {
        {type_kind::map, meta::type_name<WireT>()},
        type_info_of<key_t, Config>,
        type_info_of<mapped_t, Config>,
    };
};

template <typename WireT, typename AttrsT, typename Config>
struct type_instance_impl<WireT, AttrsT, Config, type_kind::set> {
    using element_t = std::ranges::range_value_t<WireT>;

    constexpr inline static array_type_info value = {
        {type_kind::set, meta::type_name<WireT>()},
        type_info_of<element_t, Config>,
    };
};

template <typename WireT, typename AttrsT, typename Config>
struct type_instance_impl<WireT, AttrsT, Config, type_kind::array> {
    using element_t = std::ranges::range_value_t<WireT>;

    constexpr inline static array_type_info value = {
        {type_kind::array, meta::type_name<WireT>()},
        type_info_of<element_t, Config>,
    };
};

template <typename WireT, typename AttrsT, typename Config>
struct type_instance_impl<WireT, AttrsT, Config, type_kind::structure> {
    using schema_config = struct_schema_config_t<Config, AttrsT>;
    constexpr static std::size_t count = effective_field_count<WireT>();
    constexpr static bool deny_unknown =
        has_deny_unknown_fields<WireT>() || tuple_has_v<AttrsT, attrs::deny_unknown_fields>;
    constexpr static bool is_trivially_copyable = std::is_trivially_copyable_v<WireT>;

    constexpr inline static built_fields_t<WireT, schema_config> fields =
        build_fields<WireT, schema_config>();

    constexpr inline static struct_type_info value = {
        {type_kind::structure, meta::type_name<WireT>()},
        deny_unknown,
        is_trivially_copyable,
        {fields.data(),        count                   },
    };
};

template <typename WireT, typename AttrsT, typename Config>
struct type_instance_impl<WireT, AttrsT, Config, type_kind::enumeration> {
    constexpr static auto& names = meta::reflection<WireT>::member_names;
    constexpr static auto& values = meta::reflection<WireT>::member_values;
    using underlying_t = std::underlying_type_t<WireT>;

    constexpr inline static enum_type_info value = {
        {type_kind::enumeration, meta::type_name<WireT>()},
        {names.data(),           names.size()            },
        static_cast<const void*>(values.data()),
        kind_of<underlying_t>(),
    };
};

template <typename T, typename Config, std::size_t I>
constexpr void fill_field(auto& result, std::size_t& out, std::size_t base_offset);

template <typename T, typename Config, std::size_t I>
constexpr field_info make_field_info(std::size_t base_offset) {
    using field_t = meta::field_type<T, I>;
    using attrs_t = typename unwrap_annotated<field_t>::attrs;

    std::string_view name = resolve_wire_name<T, I, Config>();

    auto& alias_arr = alias_storage<T, I>::names;
    std::span<const std::string_view> aliases{alias_arr.data(), alias_arr.size()};

    std::size_t offset = base_offset + meta::field_offset<T>(I);
    constexpr bool has_default = tuple_has_v<attrs_t, attrs::default_value>;
    constexpr bool is_literal = tuple_any_of_v<attrs_t, is_literal_attr>;
    constexpr bool has_skip_if = tuple_has_spec_v<attrs_t, behavior::skip_if>;
    constexpr bool has_behavior = tuple_any_of_v<attrs_t, is_behavior_provider>;

    return field_info{
        .name = name,
        .aliases = aliases,
        .offset = offset,
        .physical_index = I,
        .type = type_info_of<field_t, Config>,
        .has_default = has_default,
        .is_literal = is_literal,
        .has_skip_if = has_skip_if,
        .has_behavior = has_behavior,
    };
}

template <typename T, typename Config>
constexpr built_fields_t<T, Config> build_fields(std::size_t base_offset) {
    built_fields_t<T, Config> result{};
    std::size_t out = 0;

    constexpr std::size_t N = meta::field_count<T>();
    if constexpr(N > 0) {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) constexpr {
            (fill_field<T, Config, Is>(result, out, base_offset), ...);
        }(std::make_index_sequence<N>{});
    }

    return result;
}

template <typename T, typename Config, std::size_t I>
constexpr void fill_field(auto& result, std::size_t& out, std::size_t base_offset) {
    using field_t = meta::field_type<T, I>;
    using attrs_t = typename unwrap_annotated<field_t>::attrs;
    constexpr bool skipped = tuple_has_v<attrs_t, attrs::skip>;
    constexpr bool flattened = tuple_has_v<attrs_t, attrs::flatten>;

    if constexpr(skipped) {
    } else if constexpr(flattened) {
        using inner_t = typename unwrap_annotated<field_t>::raw_type;
        std::size_t inner_offset = base_offset + meta::field_offset<T>(I);
        auto inner = build_fields<inner_t, Config>(inner_offset);
        for(std::size_t i = 0; i < inner.size(); ++i) {
            result[out++] = inner[i];
        }
    } else {
        result[out++] = make_field_info<T, Config, I>(base_offset);
    }
}

}  // namespace detail

template <typename T, typename Config>
constexpr const type_info& type_info_of() {
    return detail::type_instance<T, Config>::value;
}

}  // namespace kota::meta
