#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <memory>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "attribute.h"
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
    /// Whether the variant's deny_unknown_fields denies, rather than passes
    /// over, the keys beside the tag and the content (adjacent tagging), or
    /// beside the tag and an alternative's fields (internal tagging).
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

    /// Whether a document's key goes to the field: its name, or one of its aliases.
    constexpr bool answers_to(std::string_view key) const {
        return name == key || std::ranges::find(aliases, key) != aliases.end();
    }

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
    for(std::size_t i = 0; i < fields.size(); ++i) {
        for(std::size_t j = i + 1; j < fields.size(); ++j) {
            if(fields[j].answers_to(fields[i].name) ||
               std::ranges::any_of(fields[i].aliases, [&](std::string_view alias) {
                   return fields[j].answers_to(alias);
               })) {
                return false;
            }
        }
    }
    return true;
}

namespace detail {

/// Whether a node of type T can carry a tagging spec: only a std::variant, or
/// a class derived from one, has alternatives to tag.
template <typename T>
concept taggable = is_specialization_of<std::variant, T> || meta::derived_variant<T>;

/// The policy an enum_names repr spells an enum's enumerators with under Format, the scope of
/// the enum's node (node_format_t): the repr's own, else the rename_all the node scoped, the
/// field's over the enum's type annotation's, else the format's.
template <typename Repr, typename Format>
using enum_names_policy_t = std::conditional_t<std::is_void_v<typename Repr::policy>,
                                               typename Format::rename_all,
                                               typename Repr::policy>;

/// The names of E's enumerators spelled by Policy, in ascending order of value.
template <typename E, typename Policy>
constexpr const inline auto& enum_names_of = []() -> const auto& {
    constexpr const auto& names = naming::renamed<Policy, meta::reflection<E>::member_names>;
    static_assert(naming::distinct(names),
                  "enum_names: two enumerators of the enum are spelled alike");
    return names;
}();

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
            if constexpr(SpecAttr::value.rename_all != nullptr) {
                return std::type_identity<naming::rename_policy_of_t<SpecAttr::value.rename_all>>{};
            } else {
                return std::type_identity<typename Format::rename_all>{};
            }
        }())::type;
        constexpr const auto& names = naming::renamed<policy, detail::type_names<Ts...>::value>;
        static_assert(naming::distinct(names),
                      "tagged: two alternatives' types have the same name; give the tags with "
                      "tag_names");
        return names;
    }
}

/// A field a document holds, as a struct's virtual_schema lists them: its type, the attrs that
/// change how its value is read and written, and where it is declared, Owner's member Index.
template <typename RawType, typename BehaviorAttrs, typename Owner, std::size_t Index>
struct field_slot {
    using raw_type = RawType;
    using attrs = BehaviorAttrs;
    using owner = Owner;
    constexpr static std::size_t index = Index;
};

namespace detail {

/// Whether an attr changes how a field's value is read and written: as, with or a presence
/// predicate, or a struct spec that tags a variant or scopes a struct. The rest are read
/// through the field's info and stay out of its slot, so that fields alike in these share one
/// slot type.
template <typename Attr>
constexpr bool dispatch_attr_v =
    is_specialization_of<behavior::omit_if, Attr> || is_specialization_of<behavior::with, Attr> ||
    is_specialization_of<behavior::as, Attr>;

template <typename Tag>
constexpr bool dispatch_attr_v<meta::spec_attr<Tag, struct_spec>> =
    meta::spec_attr<Tag, struct_spec>::value.tagging != tag_mode::none ||
    meta::spec_attr<Tag, struct_spec>::value.rename_all != nullptr ||
    meta::spec_attr<Tag, struct_spec>::value.deny_unknown_fields ||
    meta::spec_attr<Tag, struct_spec>::value.presence_all.has_value();

/// The attrs among Attrs that dispatch_attr_v keeps.
template <typename Attrs>
struct dispatch_attrs;

template <typename... Attrs>
struct dispatch_attrs<std::tuple<Attrs...>> {
    using type = decltype(std::tuple_cat(
        std::conditional_t<dispatch_attr_v<Attrs>, std::tuple<Attrs>, std::tuple<>>{}...));
};

template <typename T,
          std::size_t I,
          bool Skipped = field_spec_of<T, I>.skip,
          bool Flattened = field_spec_of<T, I>.flatten>
struct single_field_slots {
    using type = type_list<field_slot<std::remove_cv_t<meta::field_type<T, I>>,
                                      typename dispatch_attrs<meta::field_attrs_t<T, I>>::type,
                                      T,
                                      I>>;
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

/// The slots of the fields a document holds of struct T: none for a skipped field, and those
/// of its struct for a flattened one.
template <typename T>
using build_slots_t =
    typename build_slots_from_seq<T, std::make_index_sequence<meta::field_count<T>()>>::type;

template <typename T, std::size_t I>
struct single_field_slots<T, I, /*Skipped=*/false, /*Flattened=*/true> {
    using inner_t = std::remove_cv_t<meta::field_type<T, I>>;
    static_assert(meta::reflectable_class<inner_t>,
                  "flatten requires the field type to be a reflectable struct");
    using type = build_slots_t<inner_t>;
};

/// The node whose static `value` is the info of T, a resolved type, under Format; TagAttrs
/// holds a tagged variant's tagging spec attr.
template <typename T, typename TagAttrs, typename Format, meta::type_kind Kind = meta::kind_of<T>()>
struct info_node;

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

/// A node resolved: the type the codec reads and writes for it, the tagging spec attr of a
/// tagged variant (an empty tuple otherwise), the format it is read and written under, with
/// every rename_all / deny_unknown_fields / presence_all crossed on the way scoped in, and the
/// node of its info: an enum_names enum's own, which a schema lists the names of.
template <typename T,
          typename TagAttrs,
          typename Format,
          typename Info = info_node<T, TagAttrs, Format>>
struct resolved_repr {
    using type = T;
    using tag_attrs = TagAttrs;
    using format = Format;

    /// What a schema reads of the node, keyed by the resolution rather than the annotation:
    /// two annotations that say the same (each KANNOTATE is a function of its own) resolve to
    /// one type and one scope, and share one info, and so one $defs entry. A tagged variant
    /// keeps its own tag, whose strings are no template arguments; schemas write variants
    /// inline, where distinct infos are harmless.
    constexpr const static type_info& info() {
        return Info::value;
    }
};

/// Precedence mirrors the codec dispatch (encode_with_attrs /
/// decode_with_attrs): behavior::with wins over behavior::as, which wins over
/// a tagging spec on a std::variant (a tagged variant is read and written as
/// that variant, whatever repr its type has). The type's repr applies only
/// when none of these does, and the format selects a format-scoped
/// specialization over the format-agnostic one, as the dispatch does from the
/// visitor's format. Every chosen representation re-enters the resolver, so
/// chained reprs and annotations of representation types resolve to the final
/// type, matching the codec's recursive re-dispatch on the converted value.
/// The rename_all / deny_unknown_fields / presence_all of annotated nodes
/// scope the carried format through node_format_t, as in the dispatch, so the
/// resulting type_info describes the documents the codec reads and writes. A
/// tagged variant keeps its tagging spec attr; the spec's own scope stays
/// inert for the alternatives, as in the codec, where the tagging branch is
/// taken before the scope.
template <typename T, typename Format = format>
constexpr auto resolve_repr();

/// resolve_repr over a type given as its raw type and attrs, as a struct
/// field's slot holds it.
template <typename Raw, typename Attrs, typename Format = format>
constexpr auto resolve_node() {
    // An enum's names are spelled by the scope its node makes, the field's
    // rename_all over its type's.
    using scope = node_format_t<Format, Raw, Attrs>;
    if constexpr(tuple_has_spec_v<Attrs, behavior::with>) {
        using adapter = typename tuple_find_spec_t<Attrs, behavior::with>::adapter;
        if constexpr(names_enum<adapter>) {
            return resolved_repr<std::string_view,
                                 std::tuple<>,
                                 Format,
                                 enum_info_node<Raw, enum_names_policy_t<adapter, scope>>>{};
        } else {
            return resolve_repr<declared_repr_t<adapter>, Format>();
        }
    } else if constexpr(tuple_has_spec_v<Attrs, behavior::as>) {
        using target = typename tuple_find_spec_t<Attrs, behavior::as>::target;
        return resolve_repr<target, Format>();
    } else if constexpr(taggable<Raw> && spec_of<struct_spec, Attrs>.tagging != tag_mode::none) {
        // Read and written as the variant it is, whatever repr its type has.
        using variant_t = typename decltype([] {
            if constexpr(meta::derived_variant<Raw>) {
                return std::type_identity<meta::variant_base_t<Raw>>{};
            } else {
                return std::type_identity<Raw>{};
            }
        }())::type;
        return resolved_repr<variant_t,
                             std::tuple<meta::spec_attr_t<struct_spec, Attrs>>,
                             Format>{};
    } else if constexpr(has_repr<Raw, Format>) {
        using chosen = repr_for<Raw, Format>;
        if constexpr(names_enum<chosen>) {
            return resolved_repr<std::string_view,
                                 std::tuple<>,
                                 Format,
                                 enum_info_node<Raw, enum_names_policy_t<chosen, scope>>>{};
        } else {
            return resolve_repr<declared_repr_t<chosen>, scope>();
        }
    } else if constexpr(meta::derived_variant<Raw>) {
        return resolve_node<meta::variant_base_t<Raw>, Attrs, Format>();
    } else {
        return resolved_repr<Raw, std::tuple<>, scope>{};
    }
}

template <typename T, typename Format>
constexpr auto resolve_repr() {
    return resolve_node<std::remove_cv_t<T>, meta::attrs_of_t<T>, Format>();
}

template <typename T>
using built_fields_t = std::array<field_info, type_list_size_v<build_slots_t<T>>>;

template <typename T, typename Format>
constexpr built_fields_t<T> build_fields(std::size_t base_offset = 0);

/// Whether the format denies unknown fields: a struct's deny_unknown_fields,
/// scoped in by node_format_t on the way down.
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

template <typename Variant, typename TagAttrs, typename Format>
struct variant_info_node;

template <typename TagAttrs, typename Format, typename... Ts>
struct variant_info_node<std::variant<Ts...>, TagAttrs, Format> {
    constexpr const static struct_spec& spec = spec_of<struct_spec, TagAttrs>;
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
            return resolve_tag_names<meta::spec_attr_t<struct_spec, TagAttrs>, Format, Ts...>();
        } else {
            return std::array<std::string_view, sizeof...(Ts)>{};
        }
    }();

    constexpr inline static variant_type_info value = {
        {meta::type_kind::variant, meta::type_name<std::variant<Ts...>>()},
        {alternatives.data(),      alternatives.size()                   },
        spec.tagging,
        spec.tag,
        spec.content,
        {alt_names.data(),         has_tag ? alt_names.size() : 0        },
        spec.deny_unknown_fields || format_denies_unknown<Format>(),
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

template <typename T, typename TagAttrs, typename Format, meta::type_kind Kind>
struct info_node {
    constexpr inline static type_info value = {meta::kind_of<T>(), meta::type_name<T>()};
};

template <typename T, typename TagAttrs, typename Format>
struct info_node<T, TagAttrs, Format, meta::type_kind::optional> {
    constexpr inline static optional_type_info value = {
        {meta::type_kind::optional, meta::type_name<T>()},
        type_info::of<typename T::value_type, Format>,
    };
};

template <typename T, typename TagAttrs, typename Format>
struct info_node<T, TagAttrs, Format, meta::type_kind::pointer> {
    constexpr inline static optional_type_info value = {
        {meta::type_kind::pointer, meta::type_name<T>()},
        type_info::of<typename T::element_type, Format>,
    };
};

template <typename T, typename TagAttrs, typename Format>
struct info_node<T, TagAttrs, Format, meta::type_kind::variant> :
    variant_info_node<T, TagAttrs, Format> {};

template <typename T, typename TagAttrs, typename Format>
struct info_node<T, TagAttrs, Format, meta::type_kind::tuple> : tuple_info_node<T, Format> {};

template <typename T, typename TagAttrs, typename Format>
struct info_node<T, TagAttrs, Format, meta::type_kind::map> {
    using kv_t = std::ranges::range_value_t<T>;

    constexpr inline static map_type_info value = {
        {meta::type_kind::map, meta::type_name<T>()},
        type_info::of<std::remove_const_t<typename kv_t::first_type>, Format>,
        type_info::of<typename kv_t::second_type, Format>,
    };
};

template <typename T, typename TagAttrs, typename Format>
struct info_node<T, TagAttrs, Format, meta::type_kind::set> {
    constexpr inline static array_type_info value = {
        {meta::type_kind::set, meta::type_name<T>()},
        type_info::of<std::ranges::range_value_t<T>, Format>,
    };
};

template <typename T, typename TagAttrs, typename Format>
struct info_node<T, TagAttrs, Format, meta::type_kind::array> {
    constexpr inline static array_type_info value = {
        {meta::type_kind::array, meta::type_name<T>()},
        type_info::of<std::ranges::range_value_t<T>, Format>,
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

template <typename T, typename TagAttrs, typename Format>
struct info_node<T, TagAttrs, Format, meta::type_kind::structure> {
    constexpr inline static built_fields_t<T> fields = distinct_fields(build_fields<T, Format>());

    constexpr inline static struct_type_info value = {
        {meta::type_kind::structure, meta::type_name<T>()},
        format_denies_unknown<Format>(),
        fields,
        spec_of<field_spec, meta::attrs_of_t<T>>.description,
    };
};

template <typename T, typename TagAttrs, typename Format>
struct info_node<T, TagAttrs, Format, meta::type_kind::enumeration> : enum_info_node<T> {};

template <typename T, typename Format, std::size_t I>
constexpr field_info make_field_info(std::size_t base_offset) {
    using raw_t = std::remove_cv_t<meta::field_type<T, I>>;
    using attrs_t = meta::field_attrs_t<T, I>;
    using resolved = decltype(resolve_node<raw_t, attrs_t, members_format_t<Format>>());
    constexpr const field_spec& spec = field_spec_of<T, I>;

    return field_info{
        .name = spec.rename.empty()
                    ? std::string_view(naming::renamed<typename Format::rename_all,
                                                       meta::reflection<T>::field_names>[I])
                    : spec.rename,
        .aliases = spec.alias,
        .offset = base_offset + meta::field_offset<T>(I),
        .type = &resolved::info,
        .presence = effective_presence<Format>(spec, meta::kind_of<raw_t>()),
        .description = spec.description,
        .schema_default = spec.schema_default,
        .minimum = spec.minimum,
        .maximum = spec.maximum,
        .choices = spec.choices,
        .declared_name = meta::field_name<I, T>(),
        .extensions = meta::spec_refs_of<attrs_t, field_spec, struct_spec>,
    };
}

template <typename T, typename Format, std::size_t I>
constexpr void fill_field(auto& result, std::size_t& out, std::size_t base_offset) {
    constexpr const field_spec& spec = field_spec_of<T, I>;
    if constexpr(spec.flatten) {
        // The flattened struct's fields are its own members: named and marked
        // by its own attributes, or the field's holding it, not by T's.
        using inner_t = std::remove_cv_t<meta::field_type<T, I>>;
        using inner_scope =
            node_format_t<members_format_t<Format>, inner_t, meta::field_attrs_t<T, I>>;
        for(const field_info& field:
            build_fields<inner_t, inner_scope>(base_offset + meta::field_offset<T>(I))) {
            result[out++] = field;
        }
    } else if constexpr(!spec.skip) {
        result[out++] = make_field_info<T, Format, I>(base_offset);
    }
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

}  // namespace detail

/// The representation of T as the codec dispatch resolves it: behavior attrs
/// on an annotation take precedence over the underlying type's codec::repr,
/// and chained reprs and annotations nested inside representation types are
/// followed to their final type. F selects the reprs of its format chain.
template <typename T, typename F = format>
using resolved_repr_t = typename decltype(detail::resolve_repr<std::remove_cvref_t<T>, F>())::type;

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
    static_assert(std::derived_from<F, format>, "a format extends codec::format");
    return decltype(detail::resolve_repr<T, F>())::info();
}

namespace detail {

/// The info of struct T under Format, its scope: the instance type_info::of
/// resolves T to, without scoping T's own annotation again over a field's.
template <typename T, typename Format>
constexpr const struct_type_info& struct_info() {
    return info_node<T, std::tuple<>, Format>::value;
}

}  // namespace detail

/// The fields of struct T as a document holds them under Format, the scope T
/// is read and written in: the format with T's own attributes, or the field's
/// holding it, scoped in (node_format_t).
template <typename T, typename Format = format>
    requires meta::reflectable_class<T>
struct virtual_schema {
    static_assert(std::is_same_v<resolved_repr_t<T, Format>, T>,
                  "virtual_schema: the struct travels as another type, through a codec::repr");

    // Obtain fields from the instance's value (the span inside struct_type_info::value)
    // rather than referencing info_node<T>::fields directly.  This matters for
    // recursive types: build_fields<T>() stores type_info::of<child> function pointers which
    // transitively instantiate the instance's value.  If we enter through `value` first,
    // the constexpr evaluator resolves `fields` as a sub-expression of `value` and the
    // back-reference to `value` is recognized as already-in-progress (no re-entry).
    // Entering through `fields` directly would leave `value` unevaluated, causing Clang to
    // attempt its verification and discover a circular dependency.
    constexpr static std::span<const field_info> fields = detail::struct_info<T, Format>().fields;

    using slots = detail::build_slots_t<T>;
    constexpr static bool deny_unknown = detail::struct_info<T, Format>().deny_unknown;
};

namespace detail {

/// Whether a field of struct T under Format answers to name, by its own name
/// or an alias; a monostate has no fields.
template <typename Format, typename T>
consteval bool has_field_named(std::string_view name) {
    if constexpr(std::is_same_v<T, std::monostate>) {
        return false;
    } else {
        using scope = node_format_t<Format, T, meta::attrs_of_t<T>>;
        return std::ranges::any_of(virtual_schema<T, scope>::fields,
                                   [&](const field_info& field) { return field.answers_to(name); });
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

/// A value-initialized T, one per type, which omit_if_default fields compare
/// with.
template <typename T>
const T& value_initialized() {
    const static T value = T();
    return value;
}

}  // namespace detail

/// Field I of struct T as a document holds it under F, the scope T is read and
/// written in (the format, with the attributes of T's own annotation or of the
/// field holding it scoped in): what encode and decode hand a visitor's
/// visit_field, all of it known at compile time. I counts the fields a
/// document holds, skipped ones left out and a flattened struct's spread in
/// its place.
template <typename T, std::size_t I, typename F = format>
struct field {
private:
    using schema = virtual_schema<T, F>;
    using slot = type_list_element_t<I, typename schema::slots>;

public:
    /// The field's type, and the attributes of its annotation that change how
    /// its value travels.
    using type = typename slot::raw_type;
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
            return meta::eq(of(owner), of(detail::value_initialized<T>()));
        } else if constexpr(presence == presence_kind::omit_if) {
            static_assert(tuple_has_spec_v<attrs, behavior::omit_if>,
                          "presence = omit_if takes its predicate as a type: "
                          "presence = type<Pred>");
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
