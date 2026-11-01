#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "attribute.h"
#include "context.h"
#include "format.h"
#include "repr.h"
#include "kotatsu/support/naming.h"
#include "kotatsu/support/tuple_traits.h"
#include "kotatsu/support/type_list.h"
#include "kotatsu/support/type_traits.h"
#include "kotatsu/meta/annotation.h"
#include "kotatsu/meta/compare.h"
#include "kotatsu/meta/enum.h"
#include "kotatsu/meta/struct.h"
#include "kotatsu/meta/type_kind.h"

/// What the codec knows of a type at compile time and at run time: the run-time type_info a
/// schema reads, the virtual_schema of a struct, and the pieces the encode and decode
/// dispatch share.
namespace kotatsu::codec {

struct type_info {
    meta::type_kind kind;
    std::string_view type_name;

    /// What T is under the format F: the info a schema or a command line reads, which lives as
    /// long as the program.
    template <typename T, typename F = format>
    constexpr const static type_info& of();
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
    /// The names of its enumerators, in ascending order of value: as an enum_names repr spells
    /// them when it travels through one, as declared otherwise.
    std::span<const std::string_view> member_names;
    meta::type_kind underlying_kind;
    /// Whether it travels as the names above, through an enum_names repr, rather than as its
    /// underlying integer.
    bool named = false;
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
    /// Under adjacent tagging, keys beside the tag and the content are
    /// denied, as a struct's unknown fields, rather than passed over.
    bool deny_unknown = false;
};

struct optional_type_info : type_info {
    type_info_fn inner;
};

struct field_info {
    /// The name the field is serialized under: its declared name, renamed.
    std::string_view name;
    std::span<const std::string_view> aliases;
    std::size_t offset;
    type_info_fn type;

    /// Whether the field may be left out of a document, and when a write
    /// leaves it out, under the format the info was built for.
    presence_kind presence;

    /// Documentation text from the annotation, empty when absent.
    std::string_view description = {};

    /// What a schema states beyond the type, from the annotation (see
    /// field_spec): whether it states the default, the bounds of a number,
    /// the values of a string.
    bool schema_default = true;
    schema_number minimum = {};
    schema_number maximum = {};
    std::span<const std::string_view> choices = {};

    /// The name the field is declared with.
    std::string_view declared_name = {};
    /// The specs of the extensions its annotation carries, found with extension().
    std::span<const meta::spec_ref> extensions = {};

    /// Whether an extension attached a spec of type `Spec` to the field.
    template <typename Spec>
    constexpr bool has_extension() const {
        return std::ranges::any_of(extensions,
                                   [](const meta::spec_ref& ref) { return ref.holds<Spec>(); });
    }

    /// The spec of type `Spec` an extension attached to the field; null when it has none.
    /// A constant expression asks has_extension() first: GCC compares no address of an
    /// inline variable with null there.
    template <typename Spec>
    constexpr const Spec* extension() const {
        for(const auto& ref: extensions) {
            if(ref.holds<Spec>()) {
                return &ref.get<Spec>();
            }
        }
        return nullptr;
    }
};

struct struct_type_info : type_info {
    bool deny_unknown;
    std::span<const field_info> fields;
    /// What the struct is, the `description` of its own annotation; empty when it has none.
    std::string_view description = {};
};

/// Whether no two of fields answer to one name: as they are named, renamed or
/// flattened in, or an alias.
constexpr bool fields_distinct(std::span<const field_info> fields) {
    auto answers = [](const field_info& field, std::string_view name) {
        return field.name == name || std::ranges::find(field.aliases, name) != field.aliases.end();
    };
    for(std::size_t i = 0; i < fields.size(); ++i) {
        for(std::size_t j = i + 1; j < fields.size(); ++j) {
            if(answers(fields[j], fields[i].name) ||
               std::ranges::any_of(fields[i].aliases, [&](std::string_view alias) {
                   return answers(fields[j], alias);
               })) {
                return false;
            }
        }
    }
    return true;
}

namespace detail {

template <typename T, std::size_t I, typename Policy>
struct renamed_name_static {
    constexpr static std::size_t len = std::string(Policy{}(meta::field_name<I, T>())).size();

    constexpr static auto chars = [] {
        std::string renamed(Policy{}(meta::field_name<I, T>()));
        std::array<char, len> arr{};
        for(std::size_t i = 0; i < len; ++i)
            arr[i] = renamed[i];
        return arr;
    }();

    constexpr static std::string_view value{chars.data(), chars.size()};
};

template <typename Format>
struct effective_field_rename {
    using type = naming::rename_policy::identity;
};

template <typename Format>
    requires requires { typename Format::rename_all; }
struct effective_field_rename<Format> {
    using type = typename Format::rename_all;
};

template <typename Format>
using effective_field_rename_t = typename effective_field_rename<Format>::type;

/// The policy an enum_names repr spells E's enumerators with under Format: its own, else the
/// rename_all of E's own annotation, else Format's.
template <typename Repr, typename E, typename Format>
consteval auto enum_names_policy() {
    constexpr naming::rename_fn own = struct_spec_of<meta::attrs_of_t<E>>.rename_all;
    if constexpr(!std::is_void_v<typename Repr::policy>) {
        return std::type_identity<typename Repr::policy>{};
    } else if constexpr(own != nullptr) {
        return std::type_identity<naming::rename_policy_of_t<own>>{};
    } else {
        return std::type_identity<effective_field_rename_t<Format>>{};
    }
}

template <typename Repr, typename E, typename Format>
using enum_names_policy_t = typename decltype(enum_names_policy<Repr, E, Format>())::type;

template <typename... Ts>
struct type_names {
    constexpr static std::array<std::string_view, sizeof...(Ts)> value{meta::type_name<Ts>()...};
};

}  // namespace detail

/// The names a tagged variant's alternatives travel under: the annotation's tag_names when it
/// gives them, one per alternative, else the names of the alternatives' types, spelled by the
/// annotation's rename_all, else by Format's. No two may be alike.
template <typename SpecAttr, typename Format, typename... Ts>
constexpr auto resolve_tag_names() {
    constexpr const struct_spec& spec = SpecAttr::value;
    if constexpr(!spec.tag_names.empty()) {
        static_assert(spec.tag_names.size() == sizeof...(Ts),
                      "tagged: number of custom names must match variant alternatives");
        std::array<std::string_view, sizeof...(Ts)> names{};
        std::ranges::copy_n(spec.tag_names.begin(), sizeof...(Ts), names.begin());
        static_assert(naming::distinct(spec.tag_names), "tagged: two tag_names are alike");
        return names;
    } else {
        using policy = typename decltype([] {
            if constexpr(spec.rename_all != nullptr) {
                return std::type_identity<naming::rename_policy_of_t<spec.rename_all>>{};
            } else {
                return std::type_identity<detail::effective_field_rename_t<Format>>{};
            }
        }())::type;
        constexpr const auto& names = naming::renamed<policy, detail::type_names<Ts...>::value>;
        static_assert(naming::distinct(names),
                      "tagged: two alternatives' types have the same name; give the tags with "
                      "tag_names");
        return names;
    }
}

namespace detail {

/// The names of E's enumerators spelled by Policy, in ascending order of value.
template <typename E, typename Policy>
constexpr const inline auto& enum_names_of = []() -> const auto& {
    constexpr const auto& names = naming::renamed<Policy, meta::reflection<E>::member_names>;
    static_assert(naming::distinct(names),
                  "enum_names: two enumerators of the enum are spelled alike");
    return names;
}();

template <typename T, std::size_t I, typename Format>
constexpr std::string_view resolve_field_name() {
    using policy = effective_field_rename_t<Format>;
    constexpr const field_spec& spec = field_spec_of<T, I>;
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
constexpr bool is_runtime_spec_attr_v<meta::spec_attr<Tag, field_spec>> =
    meta::spec_attr<Tag, field_spec>::value.presence.has_value();

/// A struct spec whose values matter at encode/decode dispatch: the tagged
/// variant paths read the tagging mode and names from the slot attrs, and a
/// rename_all / deny_unknown_fields / presence_all spec on a field merges
/// into the format there.
template <typename Attr>
constexpr bool is_runtime_struct_spec_attr_v = false;

template <typename Tag>
constexpr bool is_runtime_struct_spec_attr_v<meta::spec_attr<Tag, struct_spec>> =
    meta::spec_attr<Tag, struct_spec>::value.tagging != tag_mode::none ||
    meta::spec_attr<Tag, struct_spec>::value.rename_all != nullptr ||
    meta::spec_attr<Tag, struct_spec>::value.deny_unknown_fields ||
    meta::spec_attr<Tag, struct_spec>::value.presence_all.has_value();

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

/// The info of enum E: as it travels as the names Policy spells, through an enum_names repr,
/// or, without a Policy, as its underlying integer.
template <typename E, typename Policy = void>
struct enum_info_node {
    constexpr static bool named = !std::is_void_v<Policy>;
    constexpr const static auto& names = []() -> const auto& {
        if constexpr(named) {
            return enum_names_of<E, Policy>;
        } else {
            return meta::reflection<E>::member_names;
        }
    }();

    constexpr inline static enum_type_info value = {
        {meta::type_kind::enumeration, meta::type_name<E>()},
        {names.data(),                 names.size()        },
        meta::kind_of<std::underlying_type_t<E>>(),
        named,
    };
};

/// A resolved representation: the type the codec ultimately reads and writes
/// for T, the tagging spec attr accompanying a tagged variant (an empty tuple
/// otherwise), the format after merging every rename_all /
/// deny_unknown_fields / presence_all crossed on the way, and, for an enum
/// an enum_names repr turns into text, the info of its members (void
/// otherwise), which a schema lists.
template <typename T, typename TagAttrs, typename Format, typename SpelledEnum = void>
struct resolved_repr {
    using type = T;
    using tag_attrs = TagAttrs;
    using format = Format;
    using spelled_enum = SpelledEnum;
};

/// Precedence mirrors the codec dispatch (encode_with_attrs /
/// decode_with_attrs): behavior::with wins over behavior::as, which wins over
/// a tagging spec on a std::variant (a
/// tagged variant is read and written as that variant, whatever repr its
/// type has). The type's repr applies only when none of these does, and the
/// format selects a format-scoped specialization over the
/// format-agnostic one, as the dispatch does from the visitor's format tag.
/// Every chosen representation re-enters the resolver, so chained reprs and
/// annotations nested inside representation types resolve to the final type,
/// matching the codec's recursive re-dispatch on the converted value. The
/// rename_all / deny_unknown_fields / presence_all of annotated nodes
/// merge into the carried format through node_format_t, the alias the
/// dispatch uses, so the resulting type_info describes the documents the codec
/// reads and writes. A tagged variant keeps its tagging spec attr; the spec's
/// own policies stay inert for the alternatives, as in the codec, where the
/// tagging branch is taken before the merge.
template <typename T, typename Format = format>
constexpr auto resolve_repr();

/// resolve_repr over a type given as its raw type and attrs, as a struct
/// field's slot holds it.
template <typename Raw, typename Attrs, typename Format = format>
constexpr auto resolve_node() {
    if constexpr(meta::derived_variant<Raw>) {
        // Read and written as the variant it is, under its own annotation.
        return resolve_node<meta::variant_base_t<Raw>, Attrs, Format>();
    } else if constexpr(tuple_has_spec_v<Attrs, behavior::with>) {
        using adapter = typename tuple_find_spec_t<Attrs, behavior::with>::adapter;
        if constexpr(names_enum<adapter>) {
            return resolved_repr<std::string_view,
                                 std::tuple<>,
                                 Format,
                                 enum_info_node<Raw, enum_names_policy_t<adapter, Raw, Format>>>{};
        } else {
            return resolve_repr<declared_repr_t<adapter>, Format>();
        }
    } else if constexpr(tuple_has_spec_v<Attrs, behavior::as>) {
        using target = typename tuple_find_spec_t<Attrs, behavior::as>::target;
        return resolve_repr<target, Format>();
    } else if constexpr(is_specialization_of<std::variant, Raw> &&
                        struct_spec_of<Attrs>.tagging != tag_mode::none) {
        return resolved_repr<Raw, std::tuple<struct_spec_attr_t<Attrs>>, Format>{};
    } else if constexpr(has_repr<Raw, Format>) {
        using chosen = repr_for<Raw, Format>;
        if constexpr(names_enum<chosen>) {
            return resolved_repr<std::string_view,
                                 std::tuple<>,
                                 Format,
                                 enum_info_node<Raw, enum_names_policy_t<chosen, Raw, Format>>>{};
        } else {
            return resolve_repr<declared_repr_t<chosen>, node_format_t<Format, Raw, Attrs>>();
        }
    } else {
        return resolved_repr<Raw, std::tuple<>, node_format_t<Format, Raw, Attrs>>{};
    }
}

template <typename T, typename Format>
constexpr auto resolve_repr() {
    return resolve_node<std::remove_cv_t<T>, meta::attrs_of_t<T>, Format>();
}

template <typename T, typename AttrsT, typename Format, meta::type_kind Kind = meta::kind_of<T>()>
struct type_instance_impl;

/// The instance key is the resolution result, not the annotated input: two
/// textually identical annotations (KANNOTATE mints a fresh tag per
/// use) resolve to the same type and the same value-parameterized scoped
/// format, so they share one type_info instance — and thus one $defs entry in
/// schema output. Tagged variant specs keep their own tag (the string
/// payloads are not structural); schema backends emit variants inline rather
/// than as shared defs, so distinct instances are harmless there.
template <typename Resolved>
struct resolved_instance :
    std::conditional_t<std::is_void_v<typename Resolved::spelled_enum>,
                       type_instance_impl<typename Resolved::type,
                                          typename Resolved::tag_attrs,
                                          typename Resolved::format>,
                       typename Resolved::spelled_enum> {};

template <typename T, typename Format = format>
struct type_instance : resolved_instance<decltype(resolve_repr<std::remove_cv_t<T>, Format>())> {};

/// The info of a node resolved as Resolved, as a type_info_fn.
template <typename Resolved>
constexpr const type_info& resolved_info() {
    return resolved_instance<Resolved>::value;
}

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
    constexpr const field_spec& spec = field_spec_of<T, I>;

    if constexpr(spec.skip) {
        return 0;
    } else if constexpr(spec.flatten) {
        using inner_t = std::remove_cv_t<field_t>;
        static_assert(meta::reflectable_class<inner_t>,
                      "flatten requires the field type to be a reflectable struct");
        return effective_field_count<inner_t>();
    } else {
        return 1;
    }
}

template <typename T>
using built_fields_t = std::array<field_info, effective_field_count<T>()>;

template <typename T, typename Format>
constexpr built_fields_t<T> build_fields(std::size_t base_offset = 0);

/// Whether the format denies unknown fields: a struct's deny_unknown_fields,
/// merged there by scoped_format_t on the way down.
template <typename Format>
constexpr bool format_denies_unknown() {
    if constexpr(requires { Format::deny_unknown_fields; }) {
        return Format::deny_unknown_fields;
    } else {
        return false;
    }
}

/// Rejects an alternative of an internally tagged variant whose fields
/// cannot sit beside the tag: one that is not a struct, or a struct that
/// travels as another type, through a codec::repr. A std::monostate is a unit
/// variant, the tag alone.
template <typename Format, typename T>
consteval void assert_internal_alternative() {
    if constexpr(!std::is_same_v<T, std::monostate>) {
        static_assert(meta::reflectable_class<T> && !meta::derived_variant<T>,
                      "internally tagged: an alternative is a struct or std::monostate");
        static_assert(!has_repr<T, Format>,
                      "internally tagged: an alternative with a codec::repr has no fields to "
                      "write beside the tag; tag the variant externally or adjacently");
    }
}

template <typename Variant, typename Format, typename AttrsTuple>
struct variant_info_node;

template <typename Format, typename AttrsTuple, typename... Ts>
struct variant_info_node<std::variant<Ts...>, Format, AttrsTuple> {
    using variant_t = std::variant<Ts...>;
    constexpr const static struct_spec& spec = struct_spec_of<AttrsTuple>;
    constexpr static bool has_tag = spec.tagging != tag_mode::none;

    constexpr static std::array<type_info_fn, sizeof...(Ts)> alternatives = {
        type_info::of<Ts, Format>...};

    // The backing array is always sizeof...(Ts) (>=1 since variant must have alternatives);
    // for non-tagged variants the elements stay default-constructed and the consumer
    // span below is explicitly given size 0, so no element is ever read.
    constexpr static auto alt_names = [] {
        if constexpr(spec.tagging == tag_mode::internal) {
            (assert_internal_alternative<Format, Ts>(), ...);
        }
        if constexpr(has_tag) {
            return resolve_tag_names<struct_spec_attr_t<AttrsTuple>, Format, Ts...>();
        } else {
            return std::array<std::string_view, sizeof...(Ts)>{};
        }
    }();

    constexpr inline static variant_type_info value = {
        {meta::type_kind::variant, meta::type_name<variant_t>()  },
        {alternatives.data(),      alternatives.size()           },
        spec.tagging,
        spec.tag,
        spec.content,
        {alt_names.data(),         has_tag ? alt_names.size() : 0},
        format_denies_unknown<Format>(),
    };
};

template <typename Tuple,
          typename Format,
          typename Seq = std::make_index_sequence<std::tuple_size_v<Tuple>>>
struct tuple_info_node;

template <typename Tuple, typename Format, std::size_t... Is>
struct tuple_info_node<Tuple, Format, std::index_sequence<Is...>> {
    constexpr inline static std::array<type_info_fn, sizeof...(Is)> elements = {
        type_info::of<std::tuple_element_t<Is, Tuple>, Format>...};

    constexpr inline static tuple_type_info value = {
        {meta::type_kind::tuple, meta::type_name<Tuple>()},
        {elements.data(),        elements.size()         },
    };
};

template <typename T, typename AttrsT, typename Format, meta::type_kind Kind>
struct type_instance_impl {
    constexpr inline static type_info value = {meta::kind_of<T>(), meta::type_name<T>()};
};

template <typename T, typename AttrsT, typename Format>
struct type_instance_impl<T, AttrsT, Format, meta::type_kind::optional> {
    using inner_t = typename T::value_type;

    constexpr inline static optional_type_info value = {
        {meta::type_kind::optional, meta::type_name<T>()},
        type_info::of<inner_t, Format>,
    };
};

template <typename T, typename AttrsT, typename Format>
struct type_instance_impl<T, AttrsT, Format, meta::type_kind::pointer> {
    using inner_t = typename T::element_type;

    constexpr inline static optional_type_info value = {
        {meta::type_kind::pointer, meta::type_name<T>()},
        type_info::of<inner_t, Format>,
    };
};

template <typename T, typename AttrsT, typename Format>
struct type_instance_impl<T, AttrsT, Format, meta::type_kind::variant> {
    constexpr inline static variant_type_info value = variant_info_node<T, Format, AttrsT>::value;
};

template <typename T, typename AttrsT, typename Format>
struct type_instance_impl<T, AttrsT, Format, meta::type_kind::tuple> {
    constexpr inline static tuple_type_info value = tuple_info_node<T, Format>::value;
};

template <typename T, typename AttrsT, typename Format>
struct type_instance_impl<T, AttrsT, Format, meta::type_kind::map> {
    using kv_t = std::ranges::range_value_t<T>;
    using key_t = std::remove_const_t<typename kv_t::first_type>;
    using mapped_t = typename kv_t::second_type;

    constexpr inline static map_type_info value = {
        {meta::type_kind::map, meta::type_name<T>()},
        type_info::of<key_t, Format>,
        type_info::of<mapped_t, Format>,
    };
};

template <typename T, typename AttrsT, typename Format>
struct type_instance_impl<T, AttrsT, Format, meta::type_kind::set> {
    using element_t = std::ranges::range_value_t<T>;

    constexpr inline static array_type_info value = {
        {meta::type_kind::set, meta::type_name<T>()},
        type_info::of<element_t, Format>,
    };
};

template <typename T, typename AttrsT, typename Format>
struct type_instance_impl<T, AttrsT, Format, meta::type_kind::array> {
    using element_t = std::ranges::range_value_t<T>;

    constexpr inline static array_type_info value = {
        {meta::type_kind::array, meta::type_name<T>()},
        type_info::of<element_t, Format>,
    };
};

/// fields, which a build fails on when two of them answer to one name (see
/// fields_distinct): a document's key would go to the first.
template <std::size_t N>
consteval std::array<field_info, N> distinct_fields(std::array<field_info, N> fields) {
    if(!fields_distinct(fields)) {
        KOTATSU_THROW("codec: two fields of a struct answer to one name");
    }
    return fields;
}

template <typename T, typename AttrsT, typename Format>
struct type_instance_impl<T, AttrsT, Format, meta::type_kind::structure> {
    constexpr static bool deny_unknown = format_denies_unknown<Format>();

    constexpr inline static built_fields_t<T> fields = distinct_fields(build_fields<T, Format>());

    constexpr inline static struct_type_info value = {
        {meta::type_kind::structure, meta::type_name<T>()},
        deny_unknown,
        fields,
        spec_of<meta::type_attrs_t<T>>.description,
    };
};

template <typename T, typename AttrsT, typename Format>
struct type_instance_impl<T, AttrsT, Format, meta::type_kind::enumeration> : enum_info_node<T> {};

template <typename T, typename Format, std::size_t I>
constexpr void fill_field(auto& result, std::size_t& out, std::size_t base_offset);

template <typename T, typename Format, std::size_t I>
constexpr field_info make_field_info(std::size_t base_offset) {
    using field_t = meta::field_type<T, I>;
    using raw_t = std::remove_cv_t<field_t>;
    using attrs_t = meta::field_attrs_t<T, I>;
    constexpr const field_spec& spec = field_spec_of<T, I>;
    constexpr meta::type_kind raw_kind = meta::kind_of<raw_t>();

    return field_info{
        .name = resolve_field_name<T, I, Format>(),
        .aliases = spec.alias,
        .offset = base_offset + meta::field_offset<T>(I),
        .type = resolved_info<decltype(resolve_node<raw_t, attrs_t, members_format_t<Format>>())>,
        .presence = effective_presence<Format>(spec, raw_kind),
        .description = spec.description,
        .schema_default = spec.schema_default,
        .minimum = spec.minimum,
        .maximum = spec.maximum,
        .choices = spec.choices,
        .declared_name = meta::field_name<I, T>(),
        .extensions = extensions_of<attrs_t>,
    };
}

template <typename T, typename Format>
constexpr built_fields_t<T> build_fields(std::size_t base_offset) {
    built_fields_t<T> result{};
    std::size_t out = 0;

    constexpr std::size_t N = meta::field_count<T>();
    if constexpr(N > 0) {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) constexpr {
            (fill_field<T, Format, Is>(result, out, base_offset), ...);
        }(std::make_index_sequence<N>{});
    }

    return result;
}

template <typename T, typename Format, std::size_t I>
constexpr void fill_field(auto& result, std::size_t& out, std::size_t base_offset) {
    using field_t = meta::field_type<T, I>;
    constexpr const field_spec& spec = field_spec_of<T, I>;

    if constexpr(spec.skip) {
    } else if constexpr(spec.flatten) {
        using inner_t = std::remove_cv_t<field_t>;
        std::size_t inner_offset = base_offset + meta::field_offset<T>(I);
        auto inner = build_fields<inner_t, Format>(inner_offset);
        for(std::size_t i = 0; i < inner.size(); ++i) {
            result[out++] = inner[i];
        }
    } else {
        result[out++] = make_field_info<T, Format, I>(base_offset);
    }
}

}  // namespace detail

/// The representation of T as the codec dispatch resolves it: behavior attrs
/// on an annotation take precedence over the underlying type's codec::repr,
/// and chained reprs and annotations nested inside representation types are
/// followed to their final type. F selects the reprs of its format chain.
template <typename T, typename F = format>
using resolved_repr_t = typename decltype(detail::resolve_repr<std::remove_cvref_t<T>, F>())::type;

/// The scope T's resolution ends with: Format, the format, with the struct
/// attributes (rename_all, deny_unknown_fields, presence_all) of the struct it
/// resolves to merged in, as the dispatch layers them while reading or writing
/// a T.
template <typename T, typename Format>
using resolved_format_t =
    typename decltype(detail::resolve_repr<std::remove_cvref_t<T>, Format>())::format;

/// True when T resolves to a std::variant whose tagging spec survived
/// resolution: external, internal, and adjacent tagging all give the value an
/// object document shape in the human-readable codecs, regardless of the
/// alternatives behind it.
template <typename T, typename F = format>
constexpr bool resolves_to_tagged_variant =
    !std::is_same_v<typename decltype(detail::resolve_repr<std::remove_cvref_t<T>, F>())::tag_attrs,
                    std::tuple<>>;

template <typename T, typename F>
constexpr const type_info& type_info::of() {
    return detail::type_instance<T, F>::value;
}

/// A field a document holds, as a struct's virtual_schema lists them: its type, the attributes
/// that change how its value travels, and where it is declared, Owner's member Index.
template <typename RawType, typename BehaviorAttrs, typename Owner, std::size_t Index>
struct field_slot {
    using raw_type = RawType;
    using attrs = BehaviorAttrs;
    using owner = Owner;
    constexpr static std::size_t index = Index;
};

namespace detail {

template <typename T,
          std::size_t I,
          bool Skipped = field_spec_of<T, I>.skip,
          bool Flattened = field_spec_of<T, I>.flatten>
struct single_field_slots {
    using field_t = std::remove_cv_t<meta::field_type<T, I>>;
    using raw_type = field_t;
    using attrs_t = meta::field_attrs_t<T, I>;

    using type = type_list<field_slot<raw_type, filter_runtime_attrs_t<attrs_t>, T, I>>;
};

template <typename T, std::size_t I, bool Flattened>
struct single_field_slots<T, I, /*Skipped=*/true, Flattened> {
    using type = type_list<>;
};

template <typename T, typename Seq>
struct build_slots_from_seq;

template <typename T, std::size_t... Is>
struct build_slots_from_seq<T, std::index_sequence<Is...>> {
    using type = type_list_concat_t<typename single_field_slots<T, Is>::type...>;
};

template <typename T>
using build_slots_t =
    typename build_slots_from_seq<T, std::make_index_sequence<meta::field_count<T>()>>::type;

template <typename T, std::size_t I>
struct single_field_slots<T, I, /*Skipped=*/false, /*Flattened=*/true> {
    using field_t = meta::field_type<T, I>;
    using inner_t = std::remove_cv_t<field_t>;
    using type = build_slots_t<inner_t>;
};

}  // namespace detail

template <typename T, typename Format = format>
    requires meta::reflectable_class<T>
struct virtual_schema {
    static_assert(std::is_same_v<resolved_repr_t<T, Format>, T>,
                  "virtual_schema: the struct travels as another type, through a codec::repr");

    // Obtain fields from type_info::of<T>().fields (the span inside struct_type_info::value)
    // rather than referencing type_instance<T>::fields directly.  This matters for recursive
    // types: build_fields<T>() stores type_info::of<child> function pointers which
    // transitively instantiate type_instance<T>::value.  If we enter through `value` first,
    // the constexpr evaluator resolves `fields` as a sub-expression of `value` and the
    // back-reference to `value` is recognized as already-in-progress (no re-entry).
    // Entering through `fields` directly would leave `value` unevaluated, causing Clang to
    // attempt its verification and discover a circular dependency.
    constexpr static std::span<const field_info> fields =
        static_cast<const struct_type_info&>(type_info::of<T, Format>()).fields;

    using slots = detail::build_slots_t<T>;
    constexpr static bool deny_unknown = detail::type_instance<T, Format>::deny_unknown;
};

namespace detail {

/// Whether a field of struct T under Format answers to name, by its own name
/// or an alias; a monostate has no fields.
template <typename Format, typename T>
consteval bool has_field_named(std::string_view name) {
    if constexpr(std::is_same_v<T, std::monostate>) {
        return false;
    } else {
        return std::ranges::any_of(virtual_schema<T, Format>::fields, [&](const field_info& field) {
            return field.name == name ||
                   std::ranges::find(field.aliases, name) != field.aliases.end();
        });
    }
}

/// Rejects an internally tagged variant one of whose alternatives has a
/// field named like the tag: its document would hold the key twice.
template <typename Format, typename SpecAttr, typename... Ts>
consteval void assert_internal_tag_fits() {
    static_assert((!has_field_named<Format, Ts>(SpecAttr::value.tag) && ...),
                  "internally tagged: an alternative has a field named like the tag, so its "
                  "document would hold the key twice");
}

/// Whether a node of type T can carry a tagging spec: only a std::variant
/// has alternatives to tag.
template <typename T>
concept taggable = is_specialization_of<std::variant, T>;

}  // namespace detail

/// Field I of struct T as a document holds it under the format F, with T's own
/// attributes merged in: what encode and decode hand a visitor's visit_field,
/// all of it known at compile time. I counts the fields a document holds,
/// skipped ones left out and a flattened struct's spread in its place.
template <typename T, std::size_t I, typename F = format>
struct field {
private:
    using schema = virtual_schema<T, F>;
    using slot = type_list_element_t<I, typename schema::slots>;

public:
    /// The field's type, and the attributes of its annotation that change how
    /// its value travels.
    using type = std::remove_cv_t<typename slot::raw_type>;
    using attrs = typename slot::attrs;

    constexpr static std::size_t index = I;
    /// What the run-time type_info says of it.
    constexpr const static field_info& info = schema::fields[I];
    /// The name it is written under, and the others a read takes for it.
    constexpr static std::string_view name = info.name;
    constexpr static std::span<const std::string_view> aliases = info.aliases;
    /// Whether it may be left out of a document, and when a write leaves it
    /// out.
    constexpr static presence_kind presence = info.presence;
    /// Its annotation's spec, composed in the struct that declares it: T, or
    /// the flattened member's struct it comes from.
    constexpr const static field_spec& spec = field_spec_of<typename slot::owner, slot::index>;

    /// Whether an extension attached a spec of type Spec to the field.
    template <typename Spec>
    constexpr static bool has() {
        return info.template has_extension<Spec>();
    }

    /// The spec of type Spec an extension attached to the field, which it
    /// must have.
    template <typename Spec>
    constexpr const static Spec& extension() {
        static_assert(has<Spec>(), "the field has no extension spec of this type");
        return *info.template extension<Spec>();
    }

    /// The field inside object, as const as object is.
    template <typename Object>
        requires std::same_as<std::remove_const_t<Object>, T>
    static auto& of(Object& object) {
        constexpr bool is_const = std::is_const_v<Object>;
        using byte_t = std::conditional_t<is_const, const std::byte, std::byte>;
        using field_t = std::conditional_t<is_const, const type, type>;
        auto* base = reinterpret_cast<byte_t*>(std::addressof(object));
        return *reinterpret_cast<field_t*>(base + info.offset);
    }

    /// Whether a write leaves the field of owner out: it is its default, the
    /// field of a value-initialized T, under presence = omit_if_default, or
    /// its predicate holds for it under presence = type<Pred>.
    static bool omitted(const T& owner) {
        if constexpr(presence == presence_kind::omit_if_default) {
            static_assert(std::is_default_constructible_v<T>,
                          "presence = omit_if_default compares a field with its value in a "
                          "value-initialized struct: the struct needs a default constructor");
            const static T defaults = T();
            return meta::eq(of(owner), of(defaults));
        } else if constexpr(presence == presence_kind::omit_if) {
            using pred = typename tuple_find_spec_t<attrs, behavior::omit_if>::predicate;
            return static_cast<bool>(pred{}(of(owner)));
        } else {
            return false;
        }
    }
};

/// A field a repr or a serialize_visit specialization writes by hand, and the
/// codec for a variant's tag and content: its place among the fields written,
/// and its name.
struct named_field {
    std::size_t index;
    std::string_view name;
};

}  // namespace kotatsu::codec
