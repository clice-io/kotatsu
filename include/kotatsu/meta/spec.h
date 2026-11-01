#pragma once

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "kotatsu/support/config.h"
#include "kotatsu/support/function_traits.h"
#include "kotatsu/support/naming.h"
#include "kotatsu/support/numeric.h"
#include "kotatsu/support/type_list.h"

namespace kotatsu::meta {

/// Built-in conditions for omitting a field during serialization. Custom
/// predicates use the `skip_if = type<Pred>` component instead.
enum class skip_when : std::uint8_t {
    never,
    /// std::optional field without a value.
    none,
    /// Container whose .empty() returns true.
    empty,
    /// Value that compares equal to a value-initialized one.
    default_value,
};

/// The names an entry lists, such as a field's aliases: a view of the ones its annotation
/// keeps, as many as it wrote.
class name_list {
public:
    constexpr name_list() = default;

    constexpr explicit name_list(std::span<const std::string_view> names) : list(names) {}

    constexpr std::span<const std::string_view> names() const {
        return list;
    }

private:
    std::span<const std::string_view> list;
};

/// A number a schema states, exact whatever its type: an integer of either
/// sign, or a floating-point value; monostate when unset.
using schema_number = std::variant<std::monostate, std::int64_t, std::uint64_t, double>;

/// a <=> b exactly, whatever their types; unordered when either is unset or
/// NaN.
constexpr std::partial_ordering compare_numbers(const schema_number& a, const schema_number& b) {
    return std::visit(
        []<typename X, typename Y>(X x, Y y) -> std::partial_ordering {
            if constexpr(std::same_as<X, std::monostate> || std::same_as<Y, std::monostate>) {
                return std::partial_ordering::unordered;
            } else if constexpr(std::same_as<X, double> && std::same_as<Y, double>) {
                return x <=> y;
            } else if constexpr(std::same_as<Y, double>) {
                return compare_exact(x, y);
            } else if constexpr(std::same_as<X, double>) {
                // y against x, turned around.
                return 0 <=> compare_exact(y, x);
            } else {
                return std::cmp_less(x, y)      ? std::partial_ordering::less
                       : std::cmp_greater(x, y) ? std::partial_ordering::greater
                                                : std::partial_ordering::equivalent;
            }
        },
        a,
        b);
}

/// The value part of a field annotation. One non-template type, built by
/// KANNOTATE at compile time; downstream code reads it through
/// field_spec_of so mangled names never contain the string payloads. Designed
/// to be attachable verbatim as a C++26 [[=...]] annotation object later.
struct field_spec {
    constexpr static std::uint32_t no_idx = 0xFFFF'FFFF;

    /// Serialized name replacing the reflected field name; empty means unset.
    std::string_view rename = {};
    /// Documentation exported by schema backends; transparent in serialized output.
    std::string_view description = {};
    /// Extra accepted names during deserialization.
    name_list alias = {};
    /// Field ordinal for index-addressed formats (flatbuffers/protobuf).
    /// Metadata only for now: exposed through field_info, no backend consumes
    /// it yet.
    std::uint32_t idx = no_idx;
    /// Built-in serialization-skip condition.
    skip_when skip_if = skip_when::never;
    /// Exclude the field from (de)serialization entirely.
    bool skip = false;
    /// Inline the fields of a nested struct into the parent.
    bool flatten = false;
    /// Allow the field to be absent during deserialization: it keeps the value
    /// it had, the one its initializer gives in a value decoded fresh.
    /// Equivalent to Rust's #[serde(default)].
    bool defaulted = false;

    // What a schema states beyond the type, for its readers: the decoder
    // checks none of it.

    /// Whether a schema states the field's default value; false for one that
    /// differs between machines or runs. Whether the field is required does
    /// not change.
    bool schema_default = true;
    /// The least and the greatest value of a number field, beside the bounds
    /// of its type.
    schema_number minimum = {};
    schema_number maximum = {};
    /// The values a string field takes.
    name_list choices = {};
};

/// How a variant is tagged in serialized form.
enum class tag_mode : std::uint8_t {
    none,
    /// { "TagName": value }
    external,
    /// { "tag": "TagName", ...fields... }
    internal,
    /// { "tag": "TagName", "content": value }
    adjacent,
};

/// The value part of a struct or variant annotation. Unlike field_spec, these
/// values fork the type_info identity of the annotated type (a renamed struct
/// and its bare form have different serialized schemas).
struct struct_spec {
    /// Naming policy applied to every serialized field name.
    naming::Casing rename_all = naming::Casing::Identity;
    /// Reject unknown keys during deserialization.
    bool deny_unknown_fields = false;
    /// Allow every field to be absent during deserialization, as if each
    /// were `defaulted`.
    bool defaulted_fields = false;
    /// Variant tagging mode; derived from tagged/tag/content when the spec is built.
    tag_mode tagging = tag_mode::none;
    /// Tag field name (internal and adjacent tagging).
    std::string_view tag = {};
    /// Content field name (adjacent tagging).
    std::string_view content = {};
    /// Custom serialized names for the variant alternatives, in declaration order;
    /// empty means meta::type_name of each alternative.
    name_list tag_names = {};
};

/// Carries a type into the annotation DSL: `as = type<Target>`.
template <typename T>
struct type_tag {};

namespace dsl {

/// Which spec aspect a component sets; used to reject duplicates.
enum class aspect : std::uint8_t {
    // field_spec
    rename,
    description,
    alias,
    idx,
    skip,
    flatten,
    defaulted,
    skip_if,
    as,
    with,
    enum_string,
    schema_default,
    minimum,
    maximum,
    choices,
    // struct_spec
    rename_all,
    deny_unknown_fields,
    defaulted_fields,
    tagged,
    tag,
    content,
    tag_names,
};

/// Sets one member of a spec. The member pointer decides which spec type the
/// component applies to: field_spec, struct_spec, or the spec type of another
/// module, an extension's. `A` tells the entries of one spec type apart, so that
/// none appears twice: an aspect for meta's own, the member pointer itself for an
/// extension's (member_proxy).
template <auto A, auto Member, typename V>
struct value_component {
    V value;

    using spec_type = class_type_t<decltype(Member)>;
    constexpr static auto kind = A;

    constexpr void apply(spec_type& spec) const {
        spec.*Member = value;
    }
};

/// `tagged = true` selects external tagging; internal and adjacent tagging are
/// selected by naming the tag (and content) fields instead.
struct tagged_component {
    bool value;

    using spec_type = struct_spec;
    constexpr static aspect kind = aspect::tagged;

    constexpr void apply(struct_spec& spec) const {
        if(value) {
            spec.tagging = tag_mode::external;
        }
    }
};

/// A component that carries a type instead of a value (as/with/enum_string
/// and custom skip_if predicates). annotation.h maps it to the equivalent
/// behavior attr; the spec value is untouched.
template <aspect A, typename T>
struct type_component {
    using spec_type = field_spec;
    constexpr static aspect kind = A;

    constexpr void apply(field_spec&) const {}
};

template <typename T>
constexpr bool is_type_component_v = false;

template <aspect A, typename T>
constexpr bool is_type_component_v<type_component<A, T>> = true;

/// A DSL component: it applies to its spec_type and says which entry it is.
template <typename T>
concept component = requires(const T& component, typename T::spec_type& spec) {
    T::kind;
    component.apply(spec);
};

/// A DSL component applicable to the given spec type.
template <typename T, typename Spec>
concept spec_component = component<T> && std::same_as<typename T::spec_type, Spec>;

/// A component of an extension's spec: a spec type of another module.
template <typename T>
concept extension_component =
    component<T> && !spec_component<T, field_spec> && !spec_component<T, struct_spec>;

template <auto A, auto Member, typename V>
struct value_proxy {
    constexpr auto operator=(V value) const {
        return value_component<A, Member, V>{value};
    }
};

/// `minimum = 1`: any integer or floating-point value, kept exactly.
template <aspect A, auto Member>
struct number_proxy {
    template <typename T>
        requires (std::is_arithmetic_v<T> && !std::same_as<T, bool>)
    constexpr auto operator=(T value) const {
        schema_number number;
        if constexpr(std::is_floating_point_v<T>) {
            number = static_cast<double>(value);
        } else if constexpr(std::is_signed_v<T>) {
            number = static_cast<std::int64_t>(value);
        } else {
            number = static_cast<std::uint64_t>(value);
        }
        return value_component<A, Member, schema_number>{number};
    }
};

/// `alias = {"a", "b"}`: the names, kept in the component, which the spec views. The entries
/// of an annotation stay in static storage (make_annotation), so the view outlives the build.
template <auto A, auto Member, std::size_t N>
struct names_component {
    std::array<std::string_view, N> value;

    using spec_type = class_type_t<decltype(Member)>;
    constexpr static auto kind = A;

    constexpr void apply(spec_type& spec) const {
        spec.*Member = name_list{value};
    }
};

template <auto A, auto Member>
struct name_list_proxy {
    template <std::size_t N>
    constexpr auto operator=(const std::string_view (&names)[N]) const {
        return names_component<A, Member, N>{std::to_array(names)};
    }
};

/// An entry of an extension's spec, `Member`, which also tells it apart from
/// the spec's other entries: `constexpr inline member_proxy<&Spec::names> names{};`.
template <auto Member, typename V = member_type_t<decltype(Member)>>
using member_proxy = std::conditional_t<std::same_as<V, name_list>,
                                        name_list_proxy<Member, Member>,
                                        value_proxy<Member, Member, V>>;

struct tagged_proxy {
    constexpr auto operator=(bool value) const {
        return tagged_component{value};
    }
};

/// `skip_if` accepts both a built-in skip_when value and a custom predicate
/// type; the two forms conflict through the shared aspect.
struct skip_if_proxy {
    constexpr auto operator=(skip_when when) const {
        return value_component<aspect::skip_if, &field_spec::skip_if, skip_when>{when};
    }

    template <typename Pred>
    constexpr auto operator=(type_tag<Pred>) const {
        return type_component<aspect::skip_if, Pred>{};
    }
};

template <aspect A>
struct type_proxy {
    template <typename T>
    constexpr auto operator=(type_tag<T>) const {
        return type_component<A, T>{};
    }
};

[[maybe_unused]] constexpr inline value_proxy<aspect::rename, &field_spec::rename, std::string_view>
    rename{};
[[maybe_unused]] constexpr inline value_proxy<aspect::description,
                                              &field_spec::description,
                                              std::string_view> description{};
[[maybe_unused]] constexpr inline value_proxy<aspect::idx, &field_spec::idx, std::uint32_t> idx{};
[[maybe_unused]] constexpr inline value_proxy<aspect::skip, &field_spec::skip, bool> skip{};
[[maybe_unused]] constexpr inline value_proxy<aspect::flatten, &field_spec::flatten, bool>
    flatten{};
[[maybe_unused]] constexpr inline value_proxy<aspect::defaulted, &field_spec::defaulted, bool>
    defaulted{};
[[maybe_unused]] constexpr inline name_list_proxy<aspect::alias, &field_spec::alias> alias{};
[[maybe_unused]] constexpr inline skip_if_proxy skip_if{};
[[maybe_unused]] constexpr inline type_proxy<aspect::as> as{};
[[maybe_unused]] constexpr inline type_proxy<aspect::with> with{};
[[maybe_unused]] constexpr inline type_proxy<aspect::enum_string> enum_string{};
[[maybe_unused]] constexpr inline value_proxy<aspect::schema_default,
                                              &field_spec::schema_default,
                                              bool> schema_default{};
[[maybe_unused]] constexpr inline number_proxy<aspect::minimum, &field_spec::minimum> minimum{};
[[maybe_unused]] constexpr inline number_proxy<aspect::maximum, &field_spec::maximum> maximum{};
[[maybe_unused]] constexpr inline name_list_proxy<aspect::choices, &field_spec::choices> choices{};

[[maybe_unused]] constexpr inline value_proxy<aspect::rename_all,
                                              &struct_spec::rename_all,
                                              naming::Casing> rename_all{};
[[maybe_unused]] constexpr inline value_proxy<aspect::deny_unknown_fields,
                                              &struct_spec::deny_unknown_fields,
                                              bool> deny_unknown_fields{};
[[maybe_unused]] constexpr inline value_proxy<aspect::defaulted_fields,
                                              &struct_spec::defaulted_fields,
                                              bool> defaulted_fields{};
[[maybe_unused]] constexpr inline tagged_proxy tagged{};
[[maybe_unused]] constexpr inline value_proxy<aspect::tag, &struct_spec::tag, std::string_view>
    tag{};
[[maybe_unused]] constexpr inline value_proxy<aspect::content,
                                              &struct_spec::content,
                                              std::string_view> content{};
[[maybe_unused]] constexpr inline name_list_proxy<aspect::tag_names, &struct_spec::tag_names>
    tag_names{};

template <typename T>
[[maybe_unused]] constexpr inline type_tag<T> type{};

}  // namespace dsl

namespace detail {

/// Whether components A and B set the same entry: the same kind of the same
/// spec type.
template <typename A, typename B>
consteval bool same_entry() {
    if constexpr(std::same_as<typename A::spec_type, typename B::spec_type> &&
                 std::same_as<decltype(A::kind), decltype(B::kind)>) {
        return A::kind == B::kind;
    } else {
        return false;
    }
}

template <typename... Cs>
consteval bool component_kinds_unique() {
    std::size_t pairs = 0;
    (
        [&]<typename C>(std::type_identity<C>) {
            pairs += (std::size_t{same_entry<C, Cs>()} + ...);
        }(std::type_identity<Cs>{}),
        ...);
    // Each component sets its own entry once.
    return pairs == sizeof...(Cs);
}

/// The tuple of the distinct spec types `Cs` extend, in the order they first
/// appear.
template <typename Tuple, typename... Cs>
struct extension_specs {
    using type = Tuple;
};

template <typename... Specs, typename C, typename... Rest>
struct extension_specs<std::tuple<Specs...>, C, Rest...> {
    using spec_t = typename C::spec_type;
    constexpr static bool fresh = dsl::extension_component<C> && !is_one_of<spec_t, Specs...>;
    using type = typename extension_specs<
        std::conditional_t<fresh, std::tuple<Specs..., spec_t>, std::tuple<Specs...>>,
        Rest...>::type;
};

constexpr void validate_spec(const field_spec& spec) {
    if(spec.skip && spec.flatten) {
        KOTATSU_THROW("annotation: skip and flatten conflict");
    }
    auto aliases = spec.alias.names();
    for(std::size_t i = 0; i < aliases.size(); ++i) {
        if(aliases[i].empty()) {
            KOTATSU_THROW("annotation: empty alias name");
        }
        if(aliases[i] == spec.rename) {
            KOTATSU_THROW("annotation: alias duplicates the rename name");
        }
        for(std::size_t j = i + 1; j < aliases.size(); ++j) {
            if(aliases[i] == aliases[j]) {
                KOTATSU_THROW("annotation: duplicate alias name");
            }
        }
    }
    if(compare_numbers(spec.minimum, spec.maximum) == std::partial_ordering::greater) {
        KOTATSU_THROW("annotation: minimum exceeds maximum");
    }
    // JSON has no number for NaN or an infinity; an unbounded side is left
    // unstated.
    constexpr double largest = std::numeric_limits<double>::max();
    for(auto bound: {spec.minimum, spec.maximum}) {
        if(auto* number = std::get_if<double>(&bound);
           number && !(*number >= -largest && *number <= largest)) {
            KOTATSU_THROW("annotation: a bound that is not finite");
        }
    }
    auto choices = spec.choices.names();
    for(std::size_t i = 0; i < choices.size(); ++i) {
        for(std::size_t j = i + 1; j < choices.size(); ++j) {
            if(choices[i] == choices[j]) {
                KOTATSU_THROW("annotation: duplicate choice");
            }
        }
    }
}

/// Derive the tagging mode from the tag/content field names and check the
/// combination for consistency.
constexpr struct_spec finalize_struct_spec(struct_spec spec) {
    if(spec.tagging == tag_mode::external && !spec.tag.empty()) {
        KOTATSU_THROW("annotation: tagged = true conflicts with a tag field name");
    }
    if(!spec.content.empty()) {
        if(spec.tag.empty()) {
            KOTATSU_THROW("annotation: content requires a tag field name");
        }
        if(spec.content == spec.tag) {
            KOTATSU_THROW("annotation: tag and content field names must differ");
        }
        spec.tagging = tag_mode::adjacent;
    } else if(!spec.tag.empty()) {
        spec.tagging = tag_mode::internal;
    }

    auto names = spec.tag_names.names();
    if(!names.empty() && spec.tagging == tag_mode::none) {
        KOTATSU_THROW("annotation: tag_names requires a tagging mode");
    }
    for(std::size_t i = 0; i < names.size(); ++i) {
        if(names[i].empty()) {
            KOTATSU_THROW("annotation: empty tag name");
        }
        for(std::size_t j = i + 1; j < names.size(); ++j) {
            if(names[i] == names[j]) {
                KOTATSU_THROW("annotation: duplicate tag name");
            }
        }
    }
    return spec;
}

/// The spec of type `Spec` the entries an annotation keeps set.
template <typename Spec, typename... Cs>
constexpr Spec fold_entries(const std::tuple<Cs...>& entries) {
    Spec spec{};
    std::apply(
        [&](const Cs&... component) {
            (
                [&] {
                    if constexpr(dsl::spec_component<Cs, Spec>) {
                        component.apply(spec);
                    }
                }(),
                ...);
        },
        entries);
    return spec;
}

template <typename... Cs>
constexpr field_spec field_spec_from(const std::tuple<Cs...>& entries) {
    auto spec = fold_entries<field_spec>(entries);
    validate_spec(spec);
    return spec;
}

template <typename... Cs>
constexpr struct_spec struct_spec_from(const std::tuple<Cs...>& entries) {
    return finalize_struct_spec(fold_entries<struct_spec>(entries));
}

/// The specs of the extensions the entries set, one of each spec type, each checked by a
/// validate() of its own.
template <typename... Cs>
constexpr auto extensions_from(const std::tuple<Cs...>& entries) {
    using extensions_t = typename extension_specs<std::tuple<>, Cs...>::type;
    return [&]<typename... Specs>(std::type_identity<std::tuple<Specs...>>) {
        return extensions_t{[&] {
            auto spec = fold_entries<Specs>(entries);
            if constexpr(requires { spec.validate(); }) {
                spec.validate();
            }
            return spec;
        }()...};
    }(std::type_identity<extensions_t>{});
}

/// Whether a component sets how a variant is tagged: tagged, tag or content, which together
/// pick one mode.
template <typename C>
consteval bool sets_tagging() {
    if constexpr(std::same_as<std::remove_const_t<decltype(C::kind)>, dsl::aspect>) {
        return C::kind == dsl::aspect::tagged || C::kind == dsl::aspect::tag ||
               C::kind == dsl::aspect::content;
    } else {
        return false;
    }
}

/// Whether component `O`, of an outer annotation, overrides `I`, of an inner one: they set the
/// same entry, or both how a variant is tagged. The representation (as, with, enum_string) an
/// outer annotation picks replaces the inner one's among the attrs (kept_behavior in attrs.h).
template <typename I, typename O>
consteval bool overrides() {
    return same_entry<I, O>() || (sets_tagging<I>() && sets_tagging<O>());
}

template <typename... Os, typename I>
constexpr auto kept_entry(const I& component) {
    if constexpr((overrides<I, Os>() || ...)) {
        return std::tuple<>{};
    } else {
        return std::tuple<I>{component};
    }
}

/// The entries of an annotation written around another: the inner one's that no outer one
/// overrides, then the outer ones.
template <typename... Is, typename... Os>
constexpr auto compose_entries(const std::tuple<Is...>& inner, const std::tuple<Os...>& outer) {
    return std::apply(
        [&](const Is&... component) {
            return std::tuple_cat(kept_entry<Os...>(component)..., outer);
        },
        inner);
}

}  // namespace detail

/// The entries of one annotation, checked, as the tag it makes keeps them in its static
/// `entries`: what KANNOTATION expands to, and what a hand-written tag writes,
///
///     struct user_id_tag {
///         constexpr static auto entries = meta::make_annotation(meta::dsl::rename = "userId");
///     };
///
/// Kept as written rather than folded, so that the names they list stay where the specs view
/// them, and so that annotations compose entry by entry. Field entries make a field_spec,
/// struct entries a struct_spec, and an extension's entries its own spec; any mix may share
/// one annotation.
template <typename... Cs>
constexpr std::tuple<Cs...> make_annotation(const Cs&... components) {
    static_assert((dsl::component<Cs> && ...),
                  "annotation entries must be assignments, e.g. skip = true");
    static_assert(detail::component_kinds_unique<Cs...>(),
                  "annotation: the same attribute appears twice");
    std::tuple<Cs...> entries{components...};
    // Checked where the annotation is written, not first where a field uses it.
    detail::field_spec_from(entries);
    detail::struct_spec_from(entries);
    detail::extensions_from(entries);
    return entries;
}

}  // namespace kotatsu::meta
