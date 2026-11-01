#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "name.h"
#include "spec.h"
#include "struct.h"
#include "type_kind.h"
#include "kotatsu/support/naming.h"
#include "kotatsu/support/tuple_traits.h"
#include "kotatsu/support/type_list.h"
#include "kotatsu/support/type_traits.h"

namespace kotatsu::meta {

namespace attrs {

/// The field entries of one annotation, and its extensions' specs. Tag holds the entries in its
/// static `entries`, as make_annotation returned them; only the short tag name appears in
/// template arguments.
template <typename Tag>
struct spec {
    using tag = Tag;
    constexpr static field_spec value = detail::field_spec_from(Tag::entries);
    constexpr static auto extensions = detail::extensions_from(Tag::entries);
};

/// The struct entries of one annotation, on a struct or variant type. Unlike spec, they fork
/// the type_info identity of the annotated type: a renamed or tagged type and its bare form
/// have different serialized schemas.
template <typename Tag>
struct struct_spec {
    using tag = Tag;
    constexpr static meta::struct_spec value = detail::struct_spec_from(Tag::entries);
};

}  // namespace attrs

[[maybe_unused]] constexpr inline field_spec empty_field_spec{};

/// The value spec inside an attrs tuple; the empty spec when absent.
template <typename AttrsTuple>
constexpr const inline field_spec& spec_of = [] -> const field_spec& {
    if constexpr(tuple_has_spec_v<AttrsTuple, attrs::spec>) {
        return tuple_find_spec_t<AttrsTuple, attrs::spec>::value;
    } else {
        return empty_field_spec;
    }
}();

/// The key an extension's spec type is found by: its qualified name, which compares in a
/// constant expression too, where the address of a variable may not.
template <typename Spec>
constexpr inline std::string_view extension_key = type_name<Spec>(true);

/// An extension's spec as a field keeps it: beside the key of its type, which
/// extension_ref compares before it casts down, in a constant expression too.
struct extension_base {
    std::string_view key;
};

template <typename Spec>
struct extension_value : extension_base {
    Spec spec;
};

/// An extension's spec attached to a field: one value of a spec type of
/// another module, set by its own annotation entries.
struct extension_ref {
    const extension_base* value;

    template <typename Spec>
    constexpr bool holds() const {
        return value->key == extension_key<Spec>;
    }

    /// The spec it holds, which must be a `Spec`.
    template <typename Spec>
    constexpr const Spec& get() const {
        return static_cast<const extension_value<Spec>*>(value)->spec;
    }
};

namespace detail {

/// The extensions an attrs tuple carries, each beside its key.
template <typename AttrsTuple>
constexpr inline auto extension_values = [] {
    if constexpr(tuple_has_spec_v<AttrsTuple, attrs::spec>) {
        return std::apply(
            []<typename... Specs>(const Specs&... spec) {
                return std::tuple<extension_value<Specs>...>{
                    extension_value<Specs>{{extension_key<Specs>}, spec}
                    ...
                };
            },
            tuple_find_spec_t<AttrsTuple, attrs::spec>::extensions);
    } else {
        return std::tuple<>{};
    }
}();

template <typename AttrsTuple>
constexpr inline auto extension_refs = std::apply(
    [](const auto&... value) {
        return std::array<extension_ref, sizeof...(value)>{extension_ref{&value}...};
    },
    extension_values<AttrsTuple>);

}  // namespace detail

/// The specs of the extensions an attrs tuple carries.
template <typename AttrsTuple>
constexpr std::span<const extension_ref> extensions_of = detail::extension_refs<AttrsTuple>;

[[maybe_unused]] constexpr inline struct_spec empty_struct_spec{};

/// The struct-level spec inside an attrs tuple; the empty spec when absent.
template <typename AttrsTuple>
constexpr const inline struct_spec& struct_spec_of = [] -> const struct_spec& {
    if constexpr(tuple_has_spec_v<AttrsTuple, attrs::struct_spec>) {
        return tuple_find_spec_t<AttrsTuple, attrs::struct_spec>::value;
    } else {
        return empty_struct_spec;
    }
}();

namespace detail {

/// The layers of a merged config, one per policy: each declares its member
/// only when its policy is set, so an untouched policy keeps shining through
/// from Base.
template <typename Base, naming::Casing RenameAll>
struct rename_layer : Base {
    using field_rename = naming::rename_policy_t<RenameAll>;
};

template <typename Base>
struct rename_layer<Base, naming::Casing::Identity> : Base {};

template <typename Base, bool DenyUnknown>
struct deny_layer : Base {
    constexpr static bool deny_unknown_fields = true;
};

template <typename Base>
struct deny_layer<Base, false> : Base {};

/// A struct spec's presence_all as a template argument: 0 when unset, else the kind plus one.
constexpr std::uint8_t presence_code(std::optional<presence_kind> presence) {
    return presence ? static_cast<std::uint8_t>(static_cast<std::uint8_t>(*presence) + 1) : 0;
}

template <typename Base, std::uint8_t Presence>
struct presence_layer : Base {
    constexpr static std::optional<presence_kind> presence_all =
        static_cast<presence_kind>(Presence - 1);
};

template <typename Base>
struct presence_layer<Base, 0> : Base {};

/// Base under every policy layer: the one config type a merge makes.
template <typename Base, naming::Casing RenameAll, bool DenyUnknown, std::uint8_t Presence>
struct merged_config :
    presence_layer<deny_layer<rename_layer<Base, RenameAll>, DenyUnknown>, Presence> {};

template <typename Base, naming::Casing RenameAll, bool DenyUnknown, std::uint8_t Presence>
struct merge_config_impl {
    using type = merged_config<Base, RenameAll, DenyUnknown, Presence>;
};

template <typename Base>
struct merge_config_impl<Base, naming::Casing::Identity, false, 0> {
    using type = Base;
};

/// Merging onto an already-merged base rebuilds a single normalized layer
/// instead of stacking (a deeper rename or presence overrides, deny is
/// sticky), so equivalent merge chains produce the same config type —
/// type_info instance sharing (and thus one $defs entry per struct) depends
/// on that.
template <typename Base,
          naming::Casing R0,
          bool D0,
          std::uint8_t P0,
          naming::Casing RenameAll,
          bool DenyUnknown,
          std::uint8_t Presence>
struct merge_config_impl<merged_config<Base, R0, D0, P0>, RenameAll, DenyUnknown, Presence> {
    using type = merged_config<Base,
                               RenameAll != naming::Casing::Identity ? RenameAll : R0,
                               D0 || DenyUnknown,
                               Presence != 0 ? Presence : P0>;
};

template <typename Base, naming::Casing R0, bool D0, std::uint8_t P0>
struct merge_config_impl<merged_config<Base, R0, D0, P0>, naming::Casing::Identity, false, 0> {
    using type = merged_config<Base, R0, D0, P0>;
};

}  // namespace detail

/// Base config with an annotated node's rename_all / deny_unknown_fields /
/// presence_all layered on top; a spec carrying none of these policies
/// leaves Base untouched. A deeper merge overrides an earlier rename or
/// presence, deny is sticky: once set it is never merged away. This is the
/// single implementation of the merge — the codec dispatch applies it when
/// crossing an annotated reflectable node and meta's repr resolver replays
/// it, so type_info always describes the documents the codec reads and
/// writes.
template <typename Base, typename AttrsTuple>
using merged_config_t =
    typename detail::merge_config_impl<Base,
                                       struct_spec_of<AttrsTuple>.rename_all,
                                       struct_spec_of<AttrsTuple>.deny_unknown_fields,
                                       detail::presence_code(
                                           struct_spec_of<AttrsTuple>.presence_all)>::type;

/// The config a node of type T carrying AttrsTuple (a field's attrs, or an
/// annotation's) is read and written under: its rename_all /
/// deny_unknown_fields / presence_all merge onto Config when T is a
/// reflectable struct, the only kind those policies act on, and are inert on
/// every other node.
template <typename Config, typename T, typename AttrsTuple>
using node_config_t =
    std::conditional_t<reflectable_class<T>, merged_config_t<Config, AttrsTuple>, Config>;

/// The presence a field of kind `kind` whose spec is `spec` has under Config: the spec's own,
/// else Config's presence_all (a struct's, merged on the way, or the config's own), else
/// optional for a std::optional, a pointer or null, and required for the rest.
template <typename Config>
constexpr presence_kind effective_presence(const field_spec& spec, type_kind kind) {
    if(spec.presence) {
        return *spec.presence;
    }
    if constexpr(requires { Config::presence_all; }) {
        if(Config::presence_all) {
            return *Config::presence_all;
        }
    }
    return kind == type_kind::optional || kind == type_kind::pointer || kind == type_kind::null
               ? presence_kind::optional
               : presence_kind::required;
}

/// Resolve serialized names for variant alternatives: the annotation's
/// tag_names when provided (count must match), meta::type_name of each
/// alternative otherwise.
template <typename SpecAttr, typename... Ts>
constexpr auto resolve_tag_names() {
    constexpr const struct_spec& spec = SpecAttr::value;
    if constexpr(!spec.tag_names.empty()) {
        static_assert(spec.tag_names.size() == sizeof...(Ts),
                      "tagged: number of custom names must match variant alternatives");
        std::array<std::string_view, sizeof...(Ts)> names{};
        std::ranges::copy_n(spec.tag_names.begin(), sizeof...(Ts), names.begin());
        return names;
    } else {
        return std::array<std::string_view, sizeof...(Ts)>{type_name<Ts>()...};
    }
}

namespace behavior {

template <typename Policy>
struct enum_string {
    using policy = Policy;
};

/// Leaves a field out of what a write produces when Pred, called with the
/// field's value, returns true (`presence = type<Pred>`).
template <typename Pred>
struct omit_if {
    using predicate = Pred;
};

/// Adapter-based serialization. The adapter follows the meta::repr member
/// protocol (see repr.h), bound per-field instead of per-type; a field's
/// `with` adapter takes priority over its type's repr.
template <typename Adapter>
struct with {
    using adapter = Adapter;
};

/// Type conversion: convert to Target type before serializing via default path.
template <typename Target>
struct as {
    using target = Target;
};

}  // namespace behavior

/// True for the closed set of behavior attributes.
template <typename T>
constexpr bool is_behavior_attr_v =
    is_specialization_of<behavior::enum_string, T> || is_specialization_of<behavior::omit_if, T> ||
    is_specialization_of<behavior::with, T> || is_specialization_of<behavior::as, T>;

/// True for behavior providers (with/as/enum_string) — at most one per field.
template <typename T>
struct is_behavior_provider {
    constexpr static bool value = is_specialization_of<behavior::with, T> ||
                                  is_specialization_of<behavior::as, T> ||
                                  is_specialization_of<behavior::enum_string, T>;
};

namespace detail {

template <template <typename> typename Attr, typename... As>
consteval std::size_t count_attrs(std::type_identity<std::tuple<As...>>) {
    return (std::size_t{is_specialization_of<Attr, As>} + ... + 0);
}

template <typename AttrsTuple>
constexpr bool validate_attrs() {
    static_assert(tuple_count_of_v<AttrsTuple, is_behavior_provider> <= 1,
                  "At most one behavior provider (with/as/enum_string) allowed per field");
    static_assert(count_attrs<attrs::spec>(std::type_identity<AttrsTuple>{}) <= 1,
                  "At most one annotation spec allowed per field");
    static_assert(count_attrs<attrs::struct_spec>(std::type_identity<AttrsTuple>{}) <= 1,
                  "At most one struct annotation spec allowed per type");
    return true;
}

/// An annotation written around another, as one tag: the inner one's entries the outer one's
/// do not override, then the outer one's.
template <typename Inner, typename Outer>
struct composed_tag {
    constexpr static auto entries = compose_entries(Inner::entries, Outer::entries);
};

/// The `Attr` (attrs::spec, attrs::struct_spec) inner attrs and outer ones make together:
/// both tags composed, or the one either has.
template <typename Inner, typename Outer, template <typename> typename Attr>
constexpr auto composed_spec_attr() {
    constexpr bool inner = tuple_has_spec_v<Inner, Attr>;
    constexpr bool outer = tuple_has_spec_v<Outer, Attr>;
    if constexpr(inner) {
        using inner_tag = typename tuple_find_spec_t<Inner, Attr>::tag;
        if constexpr(outer) {
            using outer_tag = typename tuple_find_spec_t<Outer, Attr>::tag;
            return std::tuple<Attr<composed_tag<inner_tag, outer_tag>>>{};
        } else {
            return std::tuple<Attr<inner_tag>>{};
        }
    } else if constexpr(outer) {
        return std::tuple<tuple_find_spec_t<Outer, Attr>>{};
    } else {
        return std::tuple<>{};
    }
}

/// Whether attrs say whether the value may be left out: a presence entry of either form.
template <typename AttrsTuple>
consteval bool sets_presence() {
    return spec_of<AttrsTuple>.presence.has_value();
}

/// An inner behavior attr, unless the outer attrs pick the same thing: how the value is
/// represented, or when it is skipped.
template <typename Outer, typename Attr>
constexpr auto kept_behavior() {
    if constexpr(!is_behavior_attr_v<Attr>) {
        return std::tuple<>{};
    } else if constexpr(is_specialization_of<behavior::omit_if, Attr>) {
        if constexpr(sets_presence<Outer>()) {
            return std::tuple<>{};
        } else {
            return std::tuple<Attr>{};
        }
    } else if constexpr(tuple_any_of_v<Outer, is_behavior_provider>) {
        return std::tuple<>{};
    } else {
        return std::tuple<Attr>{};
    }
}

template <typename Attr>
constexpr auto behavior_attr() {
    if constexpr(is_behavior_attr_v<Attr>) {
        return std::tuple<Attr>{};
    } else {
        return std::tuple<>{};
    }
}

template <typename Inner, typename Outer>
struct compose_attrs;

template <typename... Is, typename... Os>
struct compose_attrs<std::tuple<Is...>, std::tuple<Os...>> {
    using inner = std::tuple<Is...>;
    using outer = std::tuple<Os...>;
    using type = decltype(std::tuple_cat(composed_spec_attr<inner, outer, attrs::spec>(),
                                         composed_spec_attr<inner, outer, attrs::struct_spec>(),
                                         kept_behavior<outer, Is>()...,
                                         behavior_attr<Os>()...));
};

// An annotation over none, or none over an annotation, keeps its attrs as they are, in their
// order: the slots and type_info of an annotation that composes with nothing stay the ones
// it had alone.

template <typename... Os>
struct compose_attrs<std::tuple<>, std::tuple<Os...>> {
    using type = std::tuple<Os...>;
};

template <typename... Is>
struct compose_attrs<std::tuple<Is...>, std::tuple<>> {
    using type = std::tuple<Is...>;
};

template <>
struct compose_attrs<std::tuple<>, std::tuple<>> {
    using type = std::tuple<>;
};

}  // namespace detail

/// The attrs of an annotation written around the attrs `Inner`, `Outer` its own: an entry of
/// the outer one overrides the inner one's, and the rest of both apply.
template <typename Inner, typename Outer>
using compose_attrs_t = typename detail::compose_attrs<Inner, Outer>::type;

namespace detail {

/// Declared so that the unqualified call below finds the user's by argument-dependent
/// lookup, in the namespace of the type KANNOTATE_TYPE annotates.
void __kotatsu__type_annotation() = delete;

/// Whether KANNOTATE_TYPE annotates `T`. A class derived from it is not: a
/// std::type_identity of it converts to none of its base's.
template <typename T>
concept has_type_annotation = (std::is_class_v<T> || std::is_enum_v<T>) &&
                              requires { __kotatsu__type_annotation(std::type_identity<T>{}); };

/// Whether an entry may annotate a type, as the default of every field of that type: its
/// description, its presence, the struct or variant entries (rename_all, tagging, ...), or an
/// extension's. What a field alone has (its name, whether it is skipped or flattened, how its
/// value is represented, its schema bounds) is not.
template <typename C>
constexpr bool type_entry_v = dsl::extension_component<C> || dsl::spec_component<C, struct_spec>;

template <auto Member, typename V>
constexpr bool type_entry_v<dsl::value_component<dsl::aspect::description, Member, V>> = true;

template <auto Member, typename V>
constexpr bool type_entry_v<dsl::value_component<dsl::aspect::presence, Member, V>> = true;

template <typename Pred>
constexpr bool type_entry_v<dsl::type_component<dsl::aspect::presence, Pred>> = true;

template <typename... Cs>
consteval bool type_entries(std::type_identity<std::tuple<Cs...>>) {
    return (type_entry_v<Cs> && ...);
}

/// The behavior attr of an entry that carries a type; none for an entry that carries a value.
template <typename C>
constexpr auto type_component_attr(const C&) {
    return std::tuple<>{};
}

template <dsl::aspect A, typename T>
constexpr auto type_component_attr(const dsl::type_component<A, T>&) {
    if constexpr(A == dsl::aspect::as) {
        return std::tuple<behavior::as<T>>{};
    } else if constexpr(A == dsl::aspect::with) {
        return std::tuple<behavior::with<T>>{};
    } else if constexpr(A == dsl::aspect::presence) {
        return std::tuple<behavior::omit_if<T>>{};
    } else {
        return std::tuple<behavior::enum_string<T>>{};
    }
}

/// The attrs the entries `Cs` of the annotation `Tag` make: attrs::spec for its field and
/// extension entries, attrs::struct_spec for its struct entries, and the behavior attr of
/// each entry that carries a type.
template <typename Tag, typename... Cs>
constexpr auto tag_attrs(std::type_identity<std::tuple<Cs...>>) {
    constexpr bool field = (!dsl::spec_component<Cs, struct_spec> || ...);
    constexpr bool structure = (dsl::spec_component<Cs, struct_spec> || ...);
    using attrs_t = decltype(std::tuple_cat(
        std::conditional_t<field, std::tuple<attrs::spec<Tag>>, std::tuple<>>{},
        std::conditional_t<structure, std::tuple<attrs::struct_spec<Tag>>, std::tuple<>>{},
        decltype(type_component_attr(std::declval<const Cs&>())){}...));
    static_assert(validate_attrs<attrs_t>(), "Invalid attribute combination");
    return attrs_t{};
}

/// The attrs of the annotation `Tag` keeps in its static `entries`.
template <typename Tag>
using tag_attrs_t =
    decltype(tag_attrs<Tag>(std::type_identity<std::remove_const_t<decltype(Tag::entries)>>{}));

/// The annotation of type `T` itself, as a tag.
template <typename T>
struct type_annotation {
    constexpr static auto entries = __kotatsu__type_annotation(std::type_identity<T>{});
    static_assert(type_entries(std::type_identity<std::remove_const_t<decltype(entries)>>{}),
                  "a type annotation takes a description, a presence, struct and variant "
                  "entries, and extension entries");
};

template <typename T>
constexpr auto type_attrs() {
    if constexpr(has_type_annotation<T>) {
        return tag_attrs_t<type_annotation<T>>{};
    } else {
        return std::tuple<>{};
    }
}

}  // namespace detail

/// The attrs of type `T` itself: the annotation KANNOTATE_TYPE gives it.
template <typename T>
using type_attrs_t = decltype(detail::type_attrs<T>());

/// The attrs a value of type `T` carries wherever it is: its type's annotation.
template <typename T>
using attrs_of_t = type_attrs_t<std::remove_cv_t<T>>;

/// The spec of type `Spec` an extension attached through `AttrsTuple`; null when none did.
template <typename Spec, typename AttrsTuple>
constexpr const Spec* extension_in = [] -> const Spec* {
    if constexpr(tuple_has_spec_v<AttrsTuple, attrs::spec>) {
        using spec_attr = tuple_find_spec_t<AttrsTuple, attrs::spec>;
        if constexpr(tuple_has_v<std::remove_cvref_t<decltype(spec_attr::extensions)>, Spec>) {
            return &std::get<Spec>(spec_attr::extensions);
        }
    }
    return nullptr;
}();

}  // namespace kotatsu::meta
