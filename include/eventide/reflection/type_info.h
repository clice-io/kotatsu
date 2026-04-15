#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
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
#include "eventide/common/meta.h"
#include "eventide/common/naming.h"
#include "eventide/common/ranges.h"
#include "eventide/common/tuple_traits.h"

namespace eventide::refl {

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

template <typename T, typename Config = default_config>
constexpr const type_info* type_info_of();

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
        return std::string(input);
    }
}

template <typename T, std::size_t I, typename Policy>
struct wire_name_static {
    constexpr static std::size_t len = apply_rename_cx<Policy>(refl::field_name<I, T>()).size();

    constexpr static auto storage = [] {
        auto renamed = apply_rename_cx<Policy>(refl::field_name<I, T>());
        std::array<char, len> arr{};
        for(std::size_t i = 0; i < len; ++i)
            arr[i] = renamed[i];
        return arr;
    }();

    constexpr static std::string_view value{storage.data(), storage.size()};
};

template <typename T, std::size_t I>
consteval bool field_has_explicit_rename() {
    using field_t = refl::field_type<T, I>;
    if constexpr(!annotated_type<field_t>) {
        return false;
    } else {
        return tuple_any_of_v<typename field_t::attrs, is_rename_attr>;
    }
}

template <typename T, std::size_t I, typename Config>
consteval std::string_view resolve_wire_name() {
    if constexpr(field_has_explicit_rename<T, I>()) {
        return attrs::canonical_field_name<T, I>();
    } else if constexpr(requires { typename Config::field_rename; }) {
        using policy = typename Config::field_rename;
        if constexpr(std::is_same_v<policy, naming::rename_policy::identity>) {
            return attrs::canonical_field_name<T, I>();
        } else {
            return wire_name_static<T, I, policy>::value;
        }
    } else {
        return attrs::canonical_field_name<T, I>();
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
    using type = std::conditional_t<is_behavior_attr_v<First> || is_tagged_attr<First>::value,
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

template <typename RawType,
          typename AttrsTuple,
          bool HasAs = tuple_has_spec_v<AttrsTuple, behavior::as>,
          bool HasEnumStr = tuple_has_spec_v<AttrsTuple, behavior::enum_string>,
          bool HasWithWireType = has_with_wire_type_v<AttrsTuple>>
struct resolve_wire_type {
    using type = RawType;
};

template <typename RawType, typename AttrsTuple, bool HasEnumStr, bool HasWithWireType>
struct resolve_wire_type<RawType, AttrsTuple, /*HasAs=*/true, HasEnumStr, HasWithWireType> {
    using type = typename tuple_find_spec_t<AttrsTuple, behavior::as>::target;
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
constexpr bool has_tagged_schema_attr_v = tuple_any_of_v<AttrsTuple, is_tagged_attr>;

template <typename T, std::size_t I>
consteval bool has_alias_attr() {
    using field_t = refl::field_type<T, I>;
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

    using field_t = refl::field_type<T, I>;
    using attrs_t = typename unwrap_annotated<field_t>::attrs;
    using alias_attr = tuple_find_t<attrs_t, is_alias_attr>;

    constexpr static std::size_t count = alias_attr::names.size();
    constexpr static auto names = alias_attr::names;
};

template <typename T, std::size_t I>
consteval std::size_t single_field_count() {
    using field_t = refl::field_type<T, I>;
    using attrs_t = typename unwrap_annotated<field_t>::attrs;
    constexpr bool skipped = tuple_has_v<attrs_t, attrs::skip>;
    constexpr bool flattened = tuple_has_v<attrs_t, attrs::flatten>;

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
    constexpr static bool skipped = tuple_has_v<attrs_t, attrs::skip>;
    constexpr static bool flattened = tuple_has_v<attrs_t, attrs::flatten>;
};

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
    constexpr bool has_default = tuple_has_v<attrs_t, attrs::default_value>;
    constexpr bool is_literal = tuple_any_of_v<attrs_t, is_literal_attr>;
    constexpr bool has_skip_if = tuple_has_spec_v<attrs_t, behavior::skip_if>;
    constexpr bool has_behavior = tuple_any_of_v<attrs_t, is_behavior_provider>;

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
    using field_t = refl::field_type<T, I>;
    using attrs_t = typename unwrap_annotated<field_t>::attrs;
    constexpr bool skipped = tuple_has_v<attrs_t, attrs::skip>;
    constexpr bool flattened = tuple_has_v<attrs_t, attrs::flatten>;

    if constexpr(skipped) {
    } else if constexpr(flattened) {
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

template <typename T>
consteval bool has_deny_unknown_fields() {
    using attrs_t = typename unwrap_annotated<T>::attrs;
    return tuple_has_v<attrs_t, attrs::deny_unknown_fields>;
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

template <typename V, typename Config>
struct struct_info_node {
    constexpr static std::size_t count = effective_field_count<V>();
    const static struct_type_info value;
    const static std::array<field_info, count> fields;
    constexpr static bool is_trivially_copyable =
        std::is_trivial_v<V> && std::is_standard_layout_v<V>;
    constexpr static bool deny_unknown = has_deny_unknown_fields<V>();
};

template <typename V, typename Config, typename AttrsTuple>
struct annotated_struct_info_node {
    using schema_config = struct_schema_config_t<Config, AttrsTuple>;
    const static struct_type_info value;
    constexpr static std::size_t count = effective_field_count<V>();
    const static std::array<field_info, count> fields;
    constexpr static bool is_trivially_copyable =
        std::is_trivial_v<V> && std::is_standard_layout_v<V>;
    constexpr static bool deny_unknown = tuple_has_v<AttrsTuple, attrs::deny_unknown_fields>;
};

template <typename TagAttr>
consteval tag_mode tagged_mode_for() {
    constexpr auto strategy = tagged_strategy_of<TagAttr>;
    if constexpr(strategy == tagged_strategy::external) {
        return tag_mode::external;
    } else if constexpr(strategy == tagged_strategy::internal) {
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
    using tag_attr = tuple_find_t<AttrsTuple, is_tagged_attr>;

    constexpr static auto alt_names = resolve_tag_names<tag_attr, Ts...>();
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

template <typename T, typename Config>
constexpr const type_info* type_info_of() {
    if constexpr(!std::is_same_v<T, std::remove_cv_t<T>>) {
        return type_info_of<std::remove_cv_t<T>, Config>();
    } else if constexpr(annotated_type<T>) {
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
    } else if constexpr(std::ranges::input_range<T> && !str_like<T> && !bytes_like<T>) {
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
                {names.data(), names.size()},
                {},
                {values.data(), values.size()},
                kind_of<underlying_t>(),
            };
            return &info;
        } else {
            constexpr static auto& values = detail::enum_values_as_i64<T>::values;
            constexpr static enum_type_info info = {
                {type_kind::enumeration, refl::type_name<T>()},
                {names.data(), names.size()},
                {values.data(), values.size()},
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

namespace detail {

template <typename V, typename Config>
constexpr inline struct_type_info struct_info_node<V, Config>::value = {
    {type_kind::structure, refl::type_name<V>()},
    {fields.data(),        count               },
    is_trivially_copyable,
    deny_unknown,
};

template <typename V, typename Config>
constexpr inline std::array<field_info, struct_info_node<V, Config>::count>
    struct_info_node<V, Config>::fields = build_fields<V, Config>();

template <typename V, typename Config, typename AttrsTuple>
constexpr inline struct_type_info annotated_struct_info_node<V, Config, AttrsTuple>::value = {
    {type_kind::structure, refl::type_name<V>()},
    {fields.data(),        count               },
    is_trivially_copyable,
    deny_unknown,
};

template <typename V, typename Config, typename AttrsTuple>
constexpr inline std::array<field_info, annotated_struct_info_node<V, Config, AttrsTuple>::count>
    annotated_struct_info_node<V, Config, AttrsTuple>::fields =
        build_fields<V,
                     typename annotated_struct_info_node<V, Config, AttrsTuple>::schema_config>();

}  // namespace detail

}  // namespace eventide::refl
