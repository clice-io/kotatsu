#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "name.h"
#include "struct.h"
#include "kotatsu/support/function_traits.h"
#include "kotatsu/support/tuple_traits.h"
#include "kotatsu/support/type_list.h"
#include "kotatsu/support/type_traits.h"

/// Annotations: attributes written on a field or a type, which the module that reads them folds
/// into specs of its own. meta knows how annotations are written, found and composed, and no
/// attribute: each module defines its attributes and the spec types they set.
///
/// An attribute is a value of a type with a `spec_type`, the spec it sets, a `kind`, which tells
/// it apart from the other attributes of that spec, and `apply(spec&)`. It may name an `attr`, a
/// type it adds to the annotation's attrs (`as = type<T>`). attribute_traits says, by its kind,
/// what else it overrides and whether a type annotation may carry it.
namespace kotatsu::meta {

/// The names an attribute lists, such as a field's aliases: a view of the ones its annotation
/// keeps, as many as it wrote. A member of this type in a spec takes them as `attr = {"a", "b"}`
/// (member_proxy).
using name_list = std::span<const std::string_view>;

/// What the kinds of attributes of type `Kind` say beyond telling attributes apart. Specialize
/// it for a module's enumeration of kinds:
/// - group(kind): attributes of one group, other than none (0), override one another as a
///   whole, as the attributes that together pick how a variant is tagged;
/// - field_only(kind): whether the attribute may annotate a field alone, never a type.
template <typename Kind>
struct attribute_traits {
    constexpr static int group(Kind) {
        return 0;
    }

    constexpr static bool field_only(Kind) {
        return false;
    }
};

namespace dsl {

/// Sets member `Member` of its spec to a value. `Kind` tells the attributes of one spec apart:
/// a module's own kind, or the member pointer itself (member_proxy).
template <auto Kind, auto Member, typename V>
struct value_component {
    V value;

    using spec_type = class_type_t<decltype(Member)>;
    constexpr static auto kind = Kind;

    constexpr void apply(spec_type& spec) const {
        spec.*Member = value;
    }
};

/// `alias = {"a", "b"}`: the names, kept in the attribute, which the spec views. The attributes
/// of an annotation stay in static storage (make_annotation), so the view outlives the build.
template <auto Kind, auto Member, std::size_t N>
struct names_component {
    std::array<std::string_view, N> value;

    using spec_type = class_type_t<decltype(Member)>;
    constexpr static auto kind = Kind;

    constexpr void apply(spec_type& spec) const {
        spec.*Member = name_list{value};
    }
};

template <auto Kind, auto Member, typename V>
struct value_proxy {
    constexpr auto operator=(V value) const {
        return value_component<Kind, Member, V>{value};
    }
};

template <auto Kind, auto Member>
struct name_list_proxy {
    template <std::size_t N>
    constexpr auto operator=(const std::string_view (&names)[N]) const {
        return names_component<Kind, Member, N>{std::to_array(names)};
    }
};

/// An attribute setting `Member` of a module's spec, which also tells it apart from the spec's
/// other attributes: `constexpr inline member_proxy<&Spec::names> names{};`.
template <auto Member, typename V = member_type_t<decltype(Member)>>
using member_proxy = std::conditional_t<std::same_as<V, name_list>,
                                        name_list_proxy<Member, Member>,
                                        value_proxy<Member, Member, V>>;

/// Carries a type into an annotation: `as = type<Target>`.
template <typename T>
[[maybe_unused]] constexpr inline std::type_identity<T> type{};

/// An attribute: it applies to its spec_type and says which attribute it is.
template <typename T>
concept component = requires(const T& component, typename T::spec_type& spec) {
    T::kind;
    component.apply(spec);
};

}  // namespace dsl

namespace detail {

template <typename C>
using kind_type_t = std::remove_cv_t<decltype(C::kind)>;

/// Whether the kinds of attributes A and B tell them apart: they set specs of one type, and
/// their kinds are of one type.
template <typename A, typename B>
concept kinds_comparable = std::same_as<typename A::spec_type, typename B::spec_type> &&
                           std::same_as<kind_type_t<A>, kind_type_t<B>>;

/// Whether attributes A and B set the same thing: the same kind of the same spec type.
template <typename A, typename B>
consteval bool same_attribute() {
    if constexpr(kinds_comparable<A, B>) {
        return A::kind == B::kind;
    } else {
        return false;
    }
}

/// Whether attribute `O`, of an outer annotation, overrides `I`, of an inner one: they set the
/// same thing, or belong to one group.
template <typename I, typename O>
consteval bool overrides() {
    if constexpr(kinds_comparable<I, O>) {
        using traits = attribute_traits<kind_type_t<I>>;
        constexpr int group = traits::group(I::kind);
        return I::kind == O::kind || (group != 0 && group == traits::group(O::kind));
    } else {
        return false;
    }
}

/// Whether no two of the attributes Cs set the same thing.
template <typename... Cs>
consteval bool attributes_unique(std::type_identity<std::tuple<Cs...>>) {
    std::size_t pairs = 0;
    (
        [&]<typename C>(std::type_identity<C>) {
            pairs += (std::size_t{same_attribute<C, Cs>()} + ...);
        }(std::type_identity<Cs>{}),
        ...);
    // Each attribute sets its own thing once.
    return pairs == sizeof...(Cs);
}

template <typename... Os, typename I>
constexpr auto kept_attribute(const I& component) {
    if constexpr((overrides<I, Os>() || ...)) {
        return std::tuple<>{};
    } else {
        return std::tuple<I>{component};
    }
}

/// The attributes of an annotation written around another: the inner one's that no outer one
/// overrides, then the outer ones.
template <typename... Is, typename... Os>
constexpr auto compose_attributes(const std::tuple<Is...>& inner, const std::tuple<Os...>& outer) {
    return std::apply(
        [&](const Is&... component) {
            return std::tuple_cat(kept_attribute<Os...>(component)..., outer);
        },
        inner);
}

/// Whether an argument of make_annotation is a bundle: the attributes another make_annotation
/// returned, which KANNOTATION keeps under a name.
template <typename T>
constexpr bool is_bundle_v = is_specialization_of<std::tuple, T>;

template <typename Arg>
constexpr auto bundle_attributes(const Arg& arg) {
    if constexpr(is_bundle_v<Arg>) {
        return arg;
    } else {
        return std::tuple<>{};
    }
}

template <typename Arg>
constexpr auto direct_attributes(const Arg& arg) {
    if constexpr(is_bundle_v<Arg>) {
        return std::tuple<>{};
    } else {
        return std::tuple<Arg>{arg};
    }
}

template <typename Attributes>
constexpr auto compose_bundles(const Attributes& attributes) {
    return attributes;
}

/// The attributes the bundles make, a later bundle overriding an earlier one.
template <typename Attributes, typename Next, typename... Rest>
constexpr auto compose_bundles(const Attributes& attributes,
                               const Next& next,
                               const Rest&... rest) {
    return compose_bundles(compose_attributes(attributes, next), rest...);
}

/// The distinct spec types the attributes `Attributes`, a tuple, set, as a tuple, in the order
/// they first appear; Specs holds the ones found so far.
template <typename Attributes, typename Specs = std::tuple<>>
struct spec_types {
    using type = Specs;
};

template <typename C, typename... Rest, typename... Specs>
struct spec_types<std::tuple<C, Rest...>, std::tuple<Specs...>> {
    using spec_t = typename C::spec_type;
    using type = typename spec_types<std::tuple<Rest...>,
                                     std::conditional_t<is_one_of<spec_t, Specs...>,
                                                        std::tuple<Specs...>,
                                                        std::tuple<Specs..., spec_t>>>::type;
};

/// The spec of type `Spec` the attributes set, checked by a validate() of its own, which may
/// also derive what the attributes together say.
template <typename Spec, typename... Cs>
constexpr Spec spec_from(const std::tuple<Cs...>& attributes) {
    Spec spec{};
    std::apply(
        [&]([[maybe_unused]] const Cs&... component) {
            (
                [&] {
                    if constexpr(std::same_as<typename Cs::spec_type, Spec>) {
                        component.apply(spec);
                    }
                }(),
                ...);
        },
        attributes);
    if constexpr(requires { spec.validate(); }) {
        spec.validate();
    }
    return spec;
}

/// Checks the spec of every type the attributes set.
template <typename Attributes>
constexpr void check_specs(const Attributes& attributes) {
    [&]<typename... Specs>(std::type_identity<std::tuple<Specs...>>) {
        (spec_from<Specs>(attributes), ...);
    }(std::type_identity<typename spec_types<Attributes>::type>{});
}

}  // namespace detail

/// The attributes of one annotation, checked: what KANNOTATE, KANNOTATE_FIELD, KANNOTATE_TYPE and
/// KANNOTATION keep. An argument is an attribute (`rename = "userId"`) or a bundle, the
/// attributes a KANNOTATION names, which the attributes written beside it override, and a later
/// bundle an earlier one.
///
/// Kept as written rather than folded, so that the names they list stay where the specs view
/// them, and so that annotations compose attribute by attribute. The attributes of each spec
/// type fold into one spec of that type, and an annotation may set specs of any number of types.
template <typename... Args>
constexpr auto make_annotation(const Args&... args) {
    static_assert(((detail::is_bundle_v<Args> || dsl::component<Args>) && ...),
                  "an annotation takes attributes, e.g. skip = true, or bundles");
    // No attribute is written twice beside the bundles, whose attributes give way to them.
    static_assert(
        detail::attributes_unique(
            std::type_identity<decltype(std::tuple_cat(detail::direct_attributes(args)...))>{}),
        "annotation: the same attribute appears twice");
    // The attributes written here override the bundles', which never override one another's
    // group as the attributes of one annotation would.
    auto attributes = detail::compose_attributes(
        detail::compose_bundles(std::tuple<>{}, detail::bundle_attributes(args)...),
        std::tuple_cat(detail::direct_attributes(args)...));
    // Checked where the annotation is evaluated: where KANNOTATION is written,
    // and where a field or type annotation is first read.
    detail::check_specs(attributes);
    return attributes;
}

/// The spec of type `Spec` the annotation `Tag` sets, Tag keeping the attributes in its static
/// `attributes` as make_annotation returned them; only the short tag name appears in template
/// arguments.
template <typename Tag, typename Spec>
struct spec_attr {
    using spec_type = Spec;
    constexpr static Spec value = detail::spec_from<Spec>(Tag::attributes);
};

namespace detail {

template <typename T>
constexpr bool is_spec_attr_v = false;

template <typename Tag, typename Spec>
constexpr bool is_spec_attr_v<spec_attr<Tag, Spec>> = true;

template <typename Spec, typename Attrs>
struct find_spec_attr {
    using type = void;
};

template <typename Spec, typename First, typename... Rest>
struct find_spec_attr<Spec, std::tuple<First, Rest...>> {
    using type = typename find_spec_attr<Spec, std::tuple<Rest...>>::type;
};

template <typename Spec, typename Tag, typename... Rest>
struct find_spec_attr<Spec, std::tuple<spec_attr<Tag, Spec>, Rest...>> {
    using type = spec_attr<Tag, Spec>;
};

}  // namespace detail

/// The spec_attr of spec type `Spec` among Attrs; void when there is none.
template <typename Spec, typename Attrs>
using spec_attr_t = typename detail::find_spec_attr<Spec, Attrs>::type;

/// The spec of type `Spec` attached through Attrs; null when none is.
template <typename Spec, typename Attrs>
constexpr const Spec* spec_in = [] -> const Spec* {
    if constexpr(std::is_void_v<spec_attr_t<Spec, Attrs>>) {
        return nullptr;
    } else {
        return &spec_attr_t<Spec, Attrs>::value;
    }
}();

namespace detail {

/// No annotation, as a tag: what a type or a field nothing annotates has.
struct no_annotation {
    constexpr static std::tuple<> attributes{};
};

/// The annotation Outer written around Inner, as one tag: the inner one's attributes the
/// outer one's do not override, then the outer one's.
template <typename Inner, typename Outer>
struct composed_tag {
    constexpr static auto attributes = compose_attributes(Inner::attributes, Outer::attributes);
};

/// Whether the annotation Tag sets a spec of type Spec.
template <typename Tag, typename Spec>
constexpr bool sets_spec =
    tuple_has_v<typename spec_types<std::remove_const_t<decltype(Tag::attributes)>>::type, Spec>;

/// The tag the spec of type Spec is kept under when the annotation Outer is written around
/// Inner: both composed when both set one, else the one that does, whose tag it keeps, so that
/// a field shares its type's specs where its own annotation sets none of their type.
template <typename Inner, typename Outer, typename Spec>
using spec_tag_t = std::conditional_t<
    !sets_spec<Outer, Spec>,
    Inner,
    std::conditional_t<sets_spec<Inner, Spec>, composed_tag<Inner, Outer>, Outer>>;

/// The attr an attribute adds beside the specs: one that carries a type names it.
template <typename C>
constexpr auto added_attr() {
    if constexpr(requires { typename C::attr; }) {
        return std::tuple<typename C::attr>{};
    } else {
        return std::tuple<>{};
    }
}

template <typename Inner, typename Outer, typename... Specs, typename... Cs>
constexpr auto layered_attrs(std::type_identity<std::tuple<Specs...>>,
                             std::type_identity<std::tuple<Cs...>>) {
    return std::tuple_cat(std::tuple<spec_attr<spec_tag_t<Inner, Outer, Specs>, Specs>...>{},
                          added_attr<Cs>()...);
}

template <typename Inner, typename Outer>
using composed_attributes_t = decltype(compose_attributes(Inner::attributes, Outer::attributes));

/// The attrs of the annotation Outer written around Inner, either of them no_annotation: a
/// spec_attr for each spec type their attributes set, then the attrs the attributes add.
template <typename Inner, typename Outer>
using layered_attrs_t = decltype(layered_attrs<Inner, Outer>(
    std::type_identity<typename spec_types<composed_attributes_t<Inner, Outer>>::type>{},
    std::type_identity<composed_attributes_t<Inner, Outer>>{}));

/// Declared so that the unqualified call below finds the user's by argument-dependent
/// lookup, in the namespace of the type KANNOTATE_TYPE annotates.
void __kotatsu__type_annotation() = delete;

/// Whether KANNOTATE_TYPE annotates `T`. A class derived from it is not: a
/// std::type_identity of it converts to none of its base's.
template <typename T>
concept has_type_annotation = (std::is_class_v<T> || std::is_enum_v<T>) &&
                              requires { __kotatsu__type_annotation(std::type_identity<T>{}); };

/// Whether the attributes Cs may annotate a type: none annotates a field alone.
template <typename... Cs>
consteval bool type_attributes(std::type_identity<std::tuple<Cs...>>) {
    return (!attribute_traits<kind_type_t<Cs>>::field_only(Cs::kind) && ...);
}

/// The annotation of type `T` itself, as a tag.
template <typename T>
struct type_annotation {
    constexpr static auto attributes = __kotatsu__type_annotation(std::type_identity<T>{});
    static_assert(type_attributes(std::type_identity<std::remove_const_t<decltype(attributes)>>{}),
                  "a type annotation takes no attribute a field alone has, such as its name, "
                  "whether it is skipped or flattened, or how its value is represented");
};

template <typename T>
using type_annotation_t =
    std::conditional_t<has_type_annotation<T>, type_annotation<T>, no_annotation>;

}  // namespace detail

/// The attrs a value of type `T` carries wherever it is: the annotation KANNOTATE_TYPE gives
/// its type.
template <typename T>
using attrs_of_t =
    detail::layered_attrs_t<detail::type_annotation_t<std::remove_cv_t<T>>, detail::no_annotation>;

namespace detail {

/// A field's name as a template argument.
template <std::size_t N>
struct field_key_text {
    char text[N + 1] = {};

    consteval field_key_text(const char (&name)[N + 1]) {
        std::ranges::copy(name, text);
    }

    consteval explicit field_key_text(std::string_view name) {
        std::ranges::copy(name, text);
    }
};

template <std::size_t N>
field_key_text(const char (&)[N]) -> field_key_text<N - 1>;

}  // namespace detail

/// The key KANNOTATE and KANNOTATE_FIELD file a field's annotation under: the field's name.
/// It appears only in consteval functions, so it is neither emitted nor mangled.
template <detail::field_key_text Name>
struct name_key {};

namespace detail {

/// The member a KANNOTATE or KANNOTATE_FIELD argument names, `&help` or `&ns::T::help`, in
/// parentheses too, as a template argument list with a comma needs: its last name.
consteval std::string_view member_name(std::string_view spelled) {
    spelled = spelled.substr(0, spelled.find_last_not_of(") ") + 1);
    return spelled.substr(spelled.find_last_of("&: ") + 1);
}

template <field_key_text Spelled>
consteval auto member_key() {
    constexpr std::string_view name = member_name({Spelled.text, sizeof(Spelled.text) - 1});
    return name_key<field_key_text<name.size()>(name)>{};
}

/// The key of the member a KANNOTATE or KANNOTATE_FIELD argument names.
template <field_key_text Spelled>
using member_key_t = decltype(member_key<Spelled>());

/// What KANNOTATE checks its `&member` against: `member_probe{} & member` names the member as
/// an unevaluated operand may, in a static function too, so a name no member has fails.
struct member_probe {
    template <typename T>
    friend consteval bool operator&(member_probe, const T&);
};

/// Declared so that the unqualified call below finds KANNOTATE_FIELD's by argument-dependent
/// lookup, in the namespace of the type it annotates.
void __kotatsu__field_annotation() = delete;

template <typename T, std::size_t I>
consteval auto key_of() {
    constexpr std::string_view name = field_name<I, T>();
    return name_key<field_key_text<name.size()>(name)>{};
}

template <typename T, std::size_t I>
concept annotated_in_class = requires { T::__kotatsu__annotation(key_of<T, I>()); };

template <typename T, std::size_t I>
concept annotated_out_of_class =
    requires { __kotatsu__field_annotation(std::type_identity<T>{}, key_of<T, I>()); };

/// Whether argument-dependent lookup finds a KANNOTATE_FIELD of T filed under Key, which T has
/// a field of: what KANNOTATE_FIELD checks it is written where it is read.
template <typename T, typename Key>
consteval bool has_field_keyed() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
        return (std::is_same_v<decltype(key_of<T, I>()), Key> || ...);
    }(std::make_index_sequence<field_count<T>()>{});
}

template <typename T, typename Key>
concept field_annotation_found = requires {
    __kotatsu__field_annotation(std::type_identity<T>{}, Key{});
} && has_field_keyed<T, Key>();

}  // namespace detail

/// Whether KANNOTATE or KANNOTATE_FIELD annotates field I of T.
template <typename T, std::size_t I>
concept field_annotated = detail::annotated_in_class<T, I> || detail::annotated_out_of_class<T, I>;

namespace detail {

template <typename T, std::size_t I>
consteval auto field_annotation_attributes() {
    if constexpr(annotated_in_class<T, I>) {
        return T::__kotatsu__annotation(key_of<T, I>());
    } else {
        return __kotatsu__field_annotation(std::type_identity<T>{}, key_of<T, I>());
    }
}

/// The annotation KANNOTATE or KANNOTATE_FIELD gives field I of T, as a tag.
template <typename T, std::size_t I>
struct field_annotation {
    static_assert(!(annotated_in_class<T, I> && annotated_out_of_class<T, I>),
                  "a field is annotated both in its class and out of it");

    // A function, not a lambda: MSVC copies the attributes a lambda here
    // returns from uninitialized storage.
    constexpr static auto attributes = field_annotation_attributes<T, I>();
};

template <typename T, std::size_t I>
using field_annotation_t =
    std::conditional_t<field_annotated<T, I>, field_annotation<T, I>, no_annotation>;

}  // namespace detail

/// The attrs field I of T carries: those of its type's annotation, under the field's own,
/// whose attributes override them.
template <typename T, std::size_t I>
using field_attrs_t =
    detail::layered_attrs_t<detail::type_annotation_t<std::remove_cv_t<field_type<T, I>>>,
                            detail::field_annotation_t<T, I>>;

/// The key a spec type is found by: its qualified name, which compares in a constant
/// expression too, where the address of a variable may not.
template <typename Spec>
constexpr inline std::string_view spec_key = type_name<Spec>(true);

/// A spec as a field keeps it, beside the key of its type, which spec_ref compares before it
/// casts down, in a constant expression too.
struct spec_base {
    std::string_view key;
};

template <typename Spec>
struct spec_value : spec_base {
    Spec spec;
};

/// A spec attached to a field, of a type the holder need not know.
struct spec_ref {
    const spec_base* value;

    template <typename Spec>
    constexpr bool holds() const {
        return value->key == spec_key<Spec>;
    }

    /// The spec it holds, which must be a `Spec`.
    template <typename Spec>
    constexpr const Spec& get() const {
        return static_cast<const spec_value<Spec>*>(value)->spec;
    }
};

namespace detail {

template <typename Attr, typename... Excluded>
constexpr auto kept_spec_value() {
    if constexpr(is_spec_attr_v<Attr>) {
        using spec_t = typename Attr::spec_type;
        if constexpr(!is_one_of<spec_t, Excluded...>) {
            return std::tuple<spec_value<spec_t>>{
                spec_value<spec_t>{{spec_key<spec_t>}, Attr::value}
            };
        } else {
            return std::tuple<>{};
        }
    } else {
        return std::tuple<>{};
    }
}

template <typename Attrs, typename... Excluded>
struct kept_spec_values;

template <typename... Attrs, typename... Excluded>
struct kept_spec_values<std::tuple<Attrs...>, Excluded...> {
    constexpr static auto value = std::tuple_cat(kept_spec_value<Attrs, Excluded...>()...);
    constexpr static auto refs = std::apply(
        [](const auto&... spec) {
            return std::array<spec_ref, sizeof...(spec)>{spec_ref{&spec}...};
        },
        value);
};

}  // namespace detail

/// The specs Attrs carries but those of the types Excluded: a module that reads some spec
/// types itself keeps the others, of other modules, for them to find.
template <typename Attrs, typename... Excluded>
constexpr std::span<const spec_ref> spec_refs_of =
    detail::kept_spec_values<Attrs, Excluded...>::refs;

}  // namespace kotatsu::meta
