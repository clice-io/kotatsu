#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>

#include "annotation.h"
#include "enum.h"
#include "repr.h"
#include "struct.h"
#include "type_kind.h"
#include "kota/support/naming.h"
#include "kota/support/tuple_traits.h"
#include "kota/support/type_traits.h"

namespace kota::meta {

struct default_config {};

/// Carries a backend's format tag into type resolution: passed as the Config
/// of type_info_of / virtual_schema, it makes format-scoped meta::repr
/// specializations resolve the way that backend's codec dispatch does.
template <typename Format>
struct format_config {
    using format = Format;
};

namespace detail {

template <typename Config>
constexpr auto format_of_impl() {
    if constexpr(requires { typename Config::format; }) {
        return std::type_identity<typename Config::format>{};
    } else {
        return std::type_identity<void>{};
    }
}

}  // namespace detail

/// The format tag a config or visitor carries; void — the format-agnostic
/// slot — when it carries none.
template <typename Config>
using format_of_t = typename decltype(detail::format_of_impl<Config>())::type;

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
    bool has_skip_if;
    bool has_behavior;

    /// True when the declared (pre-repr-substitution) field type is nullable
    /// (optional/pointer/null). Decode accepts absence only for these, so
    /// schema requiredness follows the raw type even when a repr or behavior
    /// attr gives the field a nullable representation.
    bool nullable = false;

    /// Field ordinal for index-addressed formats; field_spec::no_idx when
    /// absent. Metadata only for now: no backend consumes it yet.
    std::uint32_t idx = field_spec::no_idx;

    /// Documentation text from the annotation, empty when absent.
    std::string_view description = {};
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
struct renamed_name_static {
    constexpr static std::size_t len = apply_rename_cx<Policy>(meta::field_name<I, T>()).size();

    constexpr static auto chars = [] {
        auto renamed = apply_rename_cx<Policy>(meta::field_name<I, T>());
        std::array<char, len> arr{};
        for(std::size_t i = 0; i < len; ++i)
            arr[i] = renamed[i];
        return arr;
    }();

    constexpr static std::string_view value{chars.data(), chars.size()};
};

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
constexpr std::string_view resolve_field_name() {
    using policy = effective_field_rename_t<Config>;
    constexpr const field_spec& spec = field_spec_of<meta::field_type<T, I>>;
    if constexpr(!spec.rename.empty()) {
        return spec.rename;
    } else if constexpr(std::is_same_v<policy, naming::rename_policy::identity>) {
        return meta::field_name<I, T>();
    } else {
        return renamed_name_static<T, I, policy>::value;
    }
}

/// True for a spec attr whose values matter beyond schema building (decode
/// default handling, encode skip conditions); name-only specs are dropped
/// from encode slots so identically-shaped fields share one slot type.
template <typename Attr>
constexpr bool is_runtime_spec_attr_v = false;

template <typename Tag>
constexpr bool is_runtime_spec_attr_v<attrs::spec<Tag>> =
    attrs::spec<Tag>::value.defaulted || attrs::spec<Tag>::value.skip_if != skip_when::never;

/// A struct spec whose values matter at encode/decode dispatch: the tagged
/// variant paths read the tagging mode and names from the slot attrs, and a
/// rename_all/deny_unknown spec on a field merges into the config there.
template <typename Attr>
constexpr bool is_runtime_struct_spec_attr_v = false;

template <typename Tag>
constexpr bool is_runtime_struct_spec_attr_v<attrs::struct_spec<Tag>> =
    attrs::struct_spec<Tag>::value.tagging != tag_mode::none ||
    attrs::struct_spec<Tag>::value.rename_all != naming::casing::identity ||
    attrs::struct_spec<Tag>::value.deny_unknown_fields;

template <typename Tuple>
struct filter_runtime_attrs;

template <>
struct filter_runtime_attrs<std::tuple<>> {
    using type = std::tuple<>;
};

template <typename First, typename... Rest>
struct filter_runtime_attrs<std::tuple<First, Rest...>> {
    using tail = typename filter_runtime_attrs<std::tuple<Rest...>>::type;
    constexpr static bool keep = is_behavior_attr_v<First> ||
                                 is_runtime_struct_spec_attr_v<First> ||
                                 is_runtime_spec_attr_v<First>;
    using type = std::conditional_t<keep,
                                    decltype(std::tuple_cat(std::declval<std::tuple<First>>(),
                                                            std::declval<tail>())),
                                    tail>;
};

template <typename Tuple>
using filter_runtime_attrs_t = typename filter_runtime_attrs<Tuple>::type;

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

/// A resolved representation: the type the codec ultimately reads and writes
/// for T, the tagging spec attr accompanying a tagged variant (an empty tuple
/// otherwise), and the config after merging every rename_all /
/// deny_unknown_fields crossed on the way.
template <typename T, typename TagAttrs, typename Config>
struct resolved_repr {
    using type = T;
    using tag_attrs = TagAttrs;
    using config = Config;
};

/// Precedence mirrors the codec dispatch (encode_with_attrs /
/// decode_with_attrs): behavior::with wins over behavior::as, which wins over
/// behavior::enum_string; the type's repr applies only when no behavior attr
/// provides the representation, and within it the config's format tag selects
/// a format-scoped specialization over the format-agnostic one — the same
/// choice the dispatch makes from the visitor's format tag. Every chosen
/// representation re-enters the resolver, so chained reprs and annotations
/// nested inside representation types resolve to the final type, matching the
/// codec's recursive re-dispatch on the converted value. The rename_all /
/// deny_unknown_fields of annotated nodes merge into the carried config
/// through node_config_t, the same alias the codec dispatch uses, so the
/// resulting type_info describes the
/// documents the codec actually reads and writes. A tagged variant keeps its
/// tagging spec attr; the spec's own rename_all/deny stay inert for the
/// alternatives, exactly as in the codec, where the tagging branch is taken
/// before the config merge.
template <typename T, typename Config = default_config>
constexpr auto resolve_repr() {
    using raw_t = typename unwrap_annotated<T>::raw_type;
    using attrs_t = typename unwrap_annotated<T>::attrs;

    if constexpr(tuple_has_spec_v<attrs_t, behavior::with>) {
        using adapter = typename tuple_find_spec_t<attrs_t, behavior::with>::adapter;
        return resolve_repr<declared_repr_t<adapter>, Config>();
    } else if constexpr(tuple_has_spec_v<attrs_t, behavior::as>) {
        using target = typename tuple_find_spec_t<attrs_t, behavior::as>::target;
        return resolve_repr<target, Config>();
    } else if constexpr(tuple_has_spec_v<attrs_t, behavior::enum_string>) {
        return resolved_repr<std::string_view, std::tuple<>, Config>{};
    } else if constexpr(has_repr<raw_t, format_of_t<Config>>) {
        using chosen = repr_for<raw_t, format_of_t<Config>>;
        return resolve_repr<declared_repr_t<chosen>, node_config_t<Config, raw_t, attrs_t>>();
    } else if constexpr(is_specialization_of<std::variant, raw_t> &&
                        struct_spec_of<attrs_t>.tagging != tag_mode::none) {
        return resolved_repr<raw_t,
                             std::tuple<tuple_find_t<attrs_t, is_struct_spec_attr>>,
                             Config>{};
    } else {
        return resolved_repr<raw_t, std::tuple<>, node_config_t<Config, raw_t, attrs_t>>{};
    }
}

template <typename T, typename AttrsT, typename Config, type_kind Kind = kind_of<T>()>
struct type_instance_impl;

/// The instance key is the resolution result, not the annotated input: two
/// textually identical annotations (KOTATSU_ANNOTATE mints a fresh tag per
/// use) resolve to the same type and the same value-parameterized merged
/// config, so they share one type_info instance — and thus one $defs entry in
/// schema output. Tagged variant specs keep their own tag (the string
/// payloads are not structural); schema backends emit variants inline rather
/// than as shared defs, so distinct instances are harmless there.
template <typename T,
          typename Config = default_config,
          typename Resolved = decltype(resolve_repr<std::remove_cv_t<T>, Config>())>
struct type_instance :
    type_instance_impl<typename Resolved::type,
                       typename Resolved::tag_attrs,
                       typename Resolved::config> {};

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
    constexpr const field_spec& spec = field_spec_of<field_t>;

    if constexpr(spec.skip) {
        return 0;
    } else if constexpr(spec.flatten) {
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
    constexpr static bool skipped = field_spec_of<meta::field_type<T, I>>.skip;
    constexpr static bool flattened = field_spec_of<meta::field_type<T, I>>.flatten;
};

template <typename T>
using built_fields_t = std::array<field_info, effective_field_count<T>()>;

template <typename T, typename Config>
constexpr built_fields_t<T> build_fields(std::size_t base_offset = 0);

/// Deny policy carried by the config itself — merged there by merged_config_t
/// on the way down, or set directly on a codec config.
template <typename Config>
constexpr bool config_denies_unknown() {
    if constexpr(requires { Config::deny_unknown_fields; }) {
        return Config::deny_unknown_fields;
    } else {
        return false;
    }
}

template <typename Variant, typename Config, typename AttrsTuple>
struct variant_info_node;

template <typename Config, typename AttrsTuple, typename... Ts>
struct variant_info_node<std::variant<Ts...>, Config, AttrsTuple> {
    using variant_t = std::variant<Ts...>;
    constexpr const static struct_spec& spec = struct_spec_of<AttrsTuple>;
    constexpr static bool has_tag = spec.tagging != tag_mode::none;

    constexpr static std::array<type_info_fn, sizeof...(Ts)> alternatives = {
        type_info_of<Ts, Config>...};

    // The backing array is always sizeof...(Ts) (>=1 since variant must have alternatives);
    // for non-tagged variants the elements stay default-constructed and the consumer
    // span below is explicitly given size 0, so no element is ever read.
    constexpr static auto alt_names = [] {
        if constexpr(has_tag) {
            return resolve_tag_names<tuple_find_t<AttrsTuple, is_struct_spec_attr>, Ts...>();
        } else {
            return std::array<std::string_view, sizeof...(Ts)>{};
        }
    }();

    constexpr inline static variant_type_info value = {
        {type_kind::variant,  meta::type_name<variant_t>()  },
        {alternatives.data(), alternatives.size()           },
        spec.tagging,
        spec.tag,
        spec.content,
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

template <typename T, typename AttrsT, typename Config, type_kind Kind>
struct type_instance_impl {
    constexpr inline static type_info value = {
        kind_of<T>(),
        meta::type_name<T>(),
    };
};

template <typename T, typename AttrsT, typename Config>
struct type_instance_impl<T, AttrsT, Config, type_kind::optional> {
    using inner_t = typename T::value_type;

    constexpr inline static optional_type_info value = {
        {type_kind::optional, meta::type_name<T>()},
        type_info_of<inner_t, Config>,
    };
};

template <typename T, typename AttrsT, typename Config>
struct type_instance_impl<T, AttrsT, Config, type_kind::pointer> {
    using inner_t = typename T::element_type;

    constexpr inline static optional_type_info value = {
        {type_kind::pointer, meta::type_name<T>()},
        type_info_of<inner_t, Config>,
    };
};

template <typename T, typename AttrsT, typename Config>
struct type_instance_impl<T, AttrsT, Config, type_kind::variant> {
    constexpr inline static variant_type_info value = variant_info_node<T, Config, AttrsT>::value;
};

template <typename T, typename AttrsT, typename Config>
struct type_instance_impl<T, AttrsT, Config, type_kind::tuple> {
    constexpr inline static tuple_type_info value = tuple_info_node<T, Config>::value;
};

template <typename T, typename AttrsT, typename Config>
struct type_instance_impl<T, AttrsT, Config, type_kind::map> {
    using kv_t = std::ranges::range_value_t<T>;
    using key_t = std::remove_const_t<typename kv_t::first_type>;
    using mapped_t = typename kv_t::second_type;

    constexpr inline static map_type_info value = {
        {type_kind::map, meta::type_name<T>()},
        type_info_of<key_t, Config>,
        type_info_of<mapped_t, Config>,
    };
};

template <typename T, typename AttrsT, typename Config>
struct type_instance_impl<T, AttrsT, Config, type_kind::set> {
    using element_t = std::ranges::range_value_t<T>;

    constexpr inline static array_type_info value = {
        {type_kind::set, meta::type_name<T>()},
        type_info_of<element_t, Config>,
    };
};

template <typename T, typename AttrsT, typename Config>
struct type_instance_impl<T, AttrsT, Config, type_kind::array> {
    using element_t = std::ranges::range_value_t<T>;

    constexpr inline static array_type_info value = {
        {type_kind::array, meta::type_name<T>()},
        type_info_of<element_t, Config>,
    };
};

template <typename T, typename AttrsT, typename Config>
struct type_instance_impl<T, AttrsT, Config, type_kind::structure> {
    constexpr static std::size_t count = effective_field_count<T>();
    constexpr static bool deny_unknown = config_denies_unknown<Config>();
    constexpr static bool is_trivially_copyable = std::is_trivially_copyable_v<T>;

    constexpr inline static built_fields_t<T> fields = build_fields<T, Config>();

    constexpr inline static struct_type_info value = {
        {type_kind::structure, meta::type_name<T>()},
        deny_unknown,
        is_trivially_copyable,
        {fields.data(),        count               },
    };
};

template <typename T, typename AttrsT, typename Config>
struct type_instance_impl<T, AttrsT, Config, type_kind::enumeration> {
    constexpr static auto& names = meta::reflection<T>::member_names;
    constexpr static auto& values = meta::reflection<T>::member_values;
    using underlying_t = std::underlying_type_t<T>;

    constexpr inline static enum_type_info value = {
        {type_kind::enumeration, meta::type_name<T>()},
        {names.data(),           names.size()        },
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
    constexpr const field_spec& spec = field_spec_of<field_t>;
    constexpr type_kind raw_kind = kind_of<typename unwrap_annotated<field_t>::raw_type>();

    return field_info{
        .name = resolve_field_name<T, I, Config>(),
        .aliases = spec.alias.names(),
        .offset = base_offset + meta::field_offset<T>(I),
        .physical_index = I,
        .type = type_info_of<field_t, Config>,
        .has_default = spec.defaulted,
        .has_skip_if =
            spec.skip_if != skip_when::never || tuple_has_spec_v<attrs_t, behavior::skip_if>,
        .has_behavior = tuple_any_of_v<attrs_t, is_behavior_provider>,
        .nullable = raw_kind == type_kind::optional || raw_kind == type_kind::pointer ||
                    raw_kind == type_kind::null,
        .idx = spec.idx,
        .description = spec.description,
    };
}

template <typename T, typename Config>
constexpr built_fields_t<T> build_fields(std::size_t base_offset) {
    built_fields_t<T> result{};
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
    constexpr const field_spec& spec = field_spec_of<field_t>;

    if constexpr(spec.skip) {
    } else if constexpr(spec.flatten) {
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

/// The representation of T as the codec dispatch resolves it: behavior attrs
/// on an annotation take precedence over the underlying type's meta::repr,
/// and chained reprs and annotations nested inside representation types are
/// followed to their final type. Format selects format-scoped repr
/// specializations; void resolves the format-agnostic view.
template <typename T, typename Format = void>
using resolved_repr_t =
    typename decltype(detail::resolve_repr<std::remove_cvref_t<T>, format_config<Format>>())::type;

/// The config T's resolution ends with: every rename_all / deny_unknown_fields
/// spec crossed on the way — a structural annotation on T itself included —
/// merged onto Config, exactly as the codec dispatch layers them while
/// reading or writing a T. Config is a codec config (as for type_info_of),
/// not a bare format tag; its format selects format-scoped reprs.
template <typename T, typename Config>
using resolved_config_t =
    typename decltype(detail::resolve_repr<std::remove_cvref_t<T>, Config>())::config;

/// True when T resolves to a std::variant whose tagging spec survived
/// resolution: external, internal, and adjacent tagging all give the value an
/// object document shape in the human-readable codecs, regardless of the
/// alternatives behind it.
template <typename T, typename Format = void>
constexpr bool resolves_to_tagged_variant =
    !std::is_same_v<typename decltype(detail::resolve_repr<std::remove_cvref_t<T>,
                                                           format_config<Format>>())::tag_attrs,
                    std::tuple<>>;

template <typename T, typename Config>
constexpr const type_info& type_info_of() {
    return detail::type_instance<T, Config>::value;
}

}  // namespace kota::meta
