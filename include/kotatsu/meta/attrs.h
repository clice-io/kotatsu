#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "name.h"
#include "spec.h"
#include "struct.h"
#include "kotatsu/support/naming.h"
#include "kotatsu/support/tuple_traits.h"
#include "kotatsu/support/type_list.h"
#include "kotatsu/support/type_traits.h"

namespace kotatsu::meta {

template <typename T>
concept annotated_type = requires {
    typename std::remove_cvref_t<T>::annotated_type;
    typename std::remove_cvref_t<T>::attrs;
};

namespace attrs {

/// The field entries of one annotation, and its extensions' specs. Tag is the struct
/// KANNOTATE generates, or one written by hand, whose static `entries` make_annotation
/// returned; only the short tag name appears in template arguments.
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

template <typename Base, bool DefaultedFields>
struct defaulted_layer : Base {
    constexpr static bool defaulted_fields = true;
};

template <typename Base>
struct defaulted_layer<Base, false> : Base {};

/// Base under every policy layer: the one config type a merge makes.
template <typename Base, naming::Casing RenameAll, bool DenyUnknown, bool DefaultedFields>
struct merged_config :
    defaulted_layer<deny_layer<rename_layer<Base, RenameAll>, DenyUnknown>, DefaultedFields> {};

template <typename Base, naming::Casing RenameAll, bool DenyUnknown, bool DefaultedFields>
struct merge_config_impl {
    using type = merged_config<Base, RenameAll, DenyUnknown, DefaultedFields>;
};

template <typename Base>
struct merge_config_impl<Base, naming::Casing::Identity, false, false> {
    using type = Base;
};

/// Merging onto an already-merged base rebuilds a single normalized layer
/// instead of stacking (deeper rename overrides, deny and defaulted are
/// sticky), so equivalent merge chains produce the same config type —
/// type_info instance sharing (and thus one $defs entry per struct) depends
/// on that.
template <typename Base,
          naming::Casing R0,
          bool D0,
          bool F0,
          naming::Casing RenameAll,
          bool DenyUnknown,
          bool DefaultedFields>
struct merge_config_impl<merged_config<Base, R0, D0, F0>, RenameAll, DenyUnknown, DefaultedFields> {
    using type = merged_config<Base,
                               RenameAll != naming::Casing::Identity ? RenameAll : R0,
                               D0 || DenyUnknown,
                               F0 || DefaultedFields>;
};

template <typename Base, naming::Casing R0, bool D0, bool F0>
struct merge_config_impl<merged_config<Base, R0, D0, F0>, naming::Casing::Identity, false, false> {
    using type = merged_config<Base, R0, D0, F0>;
};

}  // namespace detail

/// Base config with an annotated node's rename_all / deny_unknown_fields /
/// defaulted_fields layered on top; a spec carrying none of these policies
/// leaves Base untouched. A deeper merge overrides an earlier rename, deny
/// and defaulted are sticky: once set they are never merged away. This is the
/// single implementation of the merge — the codec dispatch applies it when
/// crossing an annotated reflectable node and meta's repr resolver replays
/// it, so type_info always describes the documents the codec reads and
/// writes.
template <typename Base, typename AttrsTuple>
using merged_config_t =
    typename detail::merge_config_impl<Base,
                                       struct_spec_of<AttrsTuple>.rename_all,
                                       struct_spec_of<AttrsTuple>.deny_unknown_fields,
                                       struct_spec_of<AttrsTuple>.defaulted_fields>::type;

/// The config a node of type T carrying AttrsTuple (a field's attrs, or an
/// annotation's) is read and written under: its rename_all /
/// deny_unknown_fields / defaulted_fields merge onto Config when T is a
/// reflectable struct, the only kind those policies act on, and are inert on
/// every other node.
template <typename Config, typename T, typename AttrsTuple>
using node_config_t =
    std::conditional_t<reflectable_class<T>, merged_config_t<Config, AttrsTuple>, Config>;

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

/// Omits a field when Pred holds for its value. A predicate taking
/// (const Value&, bool is_serialize) decides for encoding and decoding
/// alike; one taking only the value speaks for encoding, and a decode never
/// skips on it.
template <typename Pred>
struct skip_if {
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

template <typename Pred, typename Value>
constexpr bool evaluate_skip_predicate(const Value& value, bool is_serialize) {
    if constexpr(requires {
                     { Pred{}(value, is_serialize) } -> std::convertible_to<bool>;
                 }) {
        return static_cast<bool>(Pred{}(value, is_serialize));
    } else if constexpr(requires {
                            { Pred{}(value) } -> std::convertible_to<bool>;
                        }) {
        return is_serialize && static_cast<bool>(Pred{}(value));
    } else {
        static_assert(
            dependent_false<Pred>,
            "behavior::skip_if predicate must return bool and accept (const Value&, bool) or (const Value&)");
        return false;
    }
}

namespace pred {

struct optional_none {
    template <typename T>
    constexpr bool operator()(const std::optional<T>& value, bool is_serialize) const {
        return is_serialize && !value.has_value();
    }
};

struct empty {
    template <typename T>
    constexpr bool operator()(const T& value, bool is_serialize) const {
        if constexpr(requires { value.empty(); }) {
            return is_serialize && value.empty();
        } else {
            return false;
        }
    }
};

struct default_value {
    template <typename T>
    constexpr bool operator()(const T& value, bool is_serialize) const {
        if constexpr(requires { value == T(); }) {
            return is_serialize && static_cast<bool>(value == T());
        } else {
            return false;
        }
    }
};

}  // namespace pred

/// Evaluate a built-in skip_when condition against a field value.
template <skip_when When, typename Value>
constexpr bool evaluate_skip_when(const Value& value, bool is_serialize) {
    static_assert(When != skip_when::never);
    if constexpr(When == skip_when::none) {
        return pred::optional_none{}(value, is_serialize);
    } else if constexpr(When == skip_when::empty) {
        return pred::empty{}(value, is_serialize);
    } else {
        return pred::default_value{}(value, is_serialize);
    }
}

/// True for the closed set of behavior attributes.
template <typename T>
constexpr bool is_behavior_attr_v =
    is_specialization_of<behavior::enum_string, T> || is_specialization_of<behavior::skip_if, T> ||
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
    static_assert(!(spec_of<AttrsTuple>.skip_if != skip_when::never &&
                    tuple_has_spec_v<AttrsTuple, behavior::skip_if>),
                  "A built-in skip_if condition and a custom skip_if predicate conflict");
    return true;
}

/// An annotation written around another, as one tag: the inner one's entries the outer one's
/// do not override, then the outer one's.
template <typename Inner, typename Outer>
struct composed_tag {
    constexpr static auto entries = compose_entries(Inner::entries, Outer::entries);
};

using skip_if_entry = dsl::type_component<dsl::aspect::skip_if, void>;

template <typename... Cs>
consteval bool has_skip_if_entry(std::type_identity<std::tuple<Cs...>>) {
    return (same_entry<Cs, skip_if_entry>() || ...);
}

/// The entries of `Tag` but its skip_if, which the predicate of an annotation around it
/// replaces.
template <typename Tag>
struct without_skip_if {
    constexpr static auto entries = std::apply(
        [](const auto&... component) {
            return std::tuple_cat(kept_entry<skip_if_entry>(component)...);
        },
        Tag::entries);
};

/// The `Attr` (attrs::spec, attrs::struct_spec) inner attrs and outer ones make together:
/// both tags composed, or the one either has. An inner field spec loses its skip_if to a bare
/// skip_if predicate around it, which no outer tag carries.
template <typename Inner, typename Outer, template <typename> typename Attr>
constexpr auto composed_spec_attr() {
    constexpr bool inner = tuple_has_spec_v<Inner, Attr>;
    constexpr bool outer = tuple_has_spec_v<Outer, Attr>;
    if constexpr(inner) {
        using inner_tag = typename tuple_find_spec_t<Inner, Attr>::tag;
        using kept_tag = std::conditional_t<std::same_as<Attr<inner_tag>, attrs::spec<inner_tag>> &&
                                                tuple_has_spec_v<Outer, behavior::skip_if>,
                                            without_skip_if<inner_tag>,
                                            inner_tag>;
        if constexpr(outer) {
            using outer_tag = typename tuple_find_spec_t<Outer, Attr>::tag;
            return std::tuple<Attr<composed_tag<kept_tag, outer_tag>>>{};
        } else {
            return std::tuple<Attr<kept_tag>>{};
        }
    } else if constexpr(outer) {
        return std::tuple<tuple_find_spec_t<Outer, Attr>>{};
    } else {
        return std::tuple<>{};
    }
}

/// Whether attrs say when the value is skipped: a skip_if entry of either form, `never`
/// included, or a skip_if predicate attr.
template <typename AttrsTuple>
consteval bool sets_skip_if() {
    if constexpr(tuple_has_spec_v<AttrsTuple, behavior::skip_if>) {
        return true;
    } else if constexpr(tuple_has_spec_v<AttrsTuple, attrs::spec>) {
        using tag = typename tuple_find_spec_t<AttrsTuple, attrs::spec>::tag;
        return has_skip_if_entry(std::type_identity<std::remove_const_t<decltype(tag::entries)>>{});
    } else {
        return false;
    }
}

/// An inner behavior attr, unless the outer attrs pick the same thing: how the value is
/// represented, or when it is skipped.
template <typename Outer, typename Attr>
constexpr auto kept_behavior() {
    if constexpr(!is_behavior_attr_v<Attr>) {
        return std::tuple<>{};
    } else if constexpr(is_specialization_of<behavior::skip_if, Attr>) {
        if constexpr(sets_skip_if<Outer>()) {
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
void kotatsu_type_annotation() = delete;

/// Whether KANNOTATE_TYPE annotates `T`. A class derived from it is not: a
/// std::type_identity of it converts to none of its base's.
template <typename T>
concept has_type_annotation = (std::is_class_v<T> || std::is_enum_v<T>) &&
                              requires { kotatsu_type_annotation(std::type_identity<T>{}); };

/// Whether an entry may annotate a type: its description, or an extension's.
template <typename C>
constexpr bool type_entry_v = dsl::extension_component<C>;

template <auto Member, typename V>
constexpr bool type_entry_v<dsl::value_component<dsl::aspect::description, Member, V>> = true;

template <typename... Cs>
consteval bool type_entries(std::type_identity<std::tuple<Cs...>>) {
    return (type_entry_v<Cs> && ...);
}

/// The annotation of type `T` itself, as a tag.
template <typename T>
struct type_annotation {
    constexpr static auto entries = kotatsu_type_annotation(std::type_identity<T>{});
    static_assert(type_entries(std::type_identity<std::remove_const_t<decltype(entries)>>{}),
                  "a type annotation takes description and extension entries");
};

template <typename T>
constexpr auto type_attrs() {
    if constexpr(has_type_annotation<T>) {
        return std::tuple<attrs::spec<type_annotation<T>>>{};
    } else {
        return std::tuple<>{};
    }
}

template <typename T>
constexpr auto own_attrs() {
    if constexpr(annotated_type<T>) {
        return typename T::attrs{};
    } else {
        return std::tuple<>{};
    }
}

}  // namespace detail

/// The attrs of type `T` itself: the annotation KANNOTATE_TYPE gives it.
template <typename T>
using type_attrs_t = decltype(detail::type_attrs<T>());

namespace detail {

template <typename T>
struct raw_type {
    using type = T;
};

template <annotated_type T>
struct raw_type<T> {
    using type = typename T::annotated_type;
};

}  // namespace detail

/// The type an annotated `T` annotates, through every annotation around it; `T` itself when
/// it is not one.
template <typename T>
using raw_type_t = typename detail::raw_type<T>::type;

/// The attrs a field of type `T` carries: the annotation of the type it annotates, under the
/// annotations written around it, an outer one's entries overriding an inner one's.
template <typename T>
using attrs_of_t = compose_attrs_t<type_attrs_t<std::remove_cv_t<raw_type_t<T>>>,
                                   decltype(detail::own_attrs<T>())>;

/// The value spec a field of type `T` carries (attrs_of_t).
template <typename T>
constexpr const inline field_spec& field_spec_of = spec_of<attrs_of_t<T>>;

/// The spec of type `Spec` an extension attached to a field of type `T`, its type's own
/// annotation included (attrs_of_t); null when it has none.
template <typename Spec, typename T>
constexpr const Spec* extension_of = [] -> const Spec* {
    using attrs_t = attrs_of_t<T>;
    if constexpr(tuple_has_spec_v<attrs_t, attrs::spec>) {
        const auto& extensions = tuple_find_spec_t<attrs_t, attrs::spec>::extensions;
        if constexpr(tuple_has_v<std::remove_cvref_t<decltype(extensions)>, Spec>) {
            return &std::get<Spec>(extensions);
        }
    }
    return nullptr;
}();

}  // namespace kotatsu::meta
