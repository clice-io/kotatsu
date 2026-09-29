#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "kota/support/config.h"
#include "kota/support/naming.h"

namespace kota::meta {

/// Built-in conditions for omitting a field during serialization. Custom
/// predicates use the `skip_if = type<Pred>` component instead.
enum class skip_when : std::uint8_t {
    never,
    /// std::optional field without a value.
    none,
    /// Container whose .empty() returns true.
    empty,
    /// Value that compares equal to a default-constructed one.
    default_value,
};

/// Fixed-capacity list of alias names for one field.
struct name_list {
    constexpr static std::size_t capacity = 8;

    std::array<std::string_view, capacity> items = {};
    std::size_t count = 0;

    constexpr std::span<const std::string_view> names() const {
        return {items.data(), count};
    }
};

/// The value part of a field annotation. One non-template type, built by
/// KOTATSU_ANNOTATE at compile time; downstream code reads it through
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
    /// Allow the field to be absent during deserialization (keeps its
    /// default-constructed value). Equivalent to Rust's #[serde(default)].
    bool defaulted = false;
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
    /// Variant tagging mode; derived from tagged/tag/content by make_struct_spec.
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
    // struct_spec
    rename_all,
    deny_unknown_fields,
    tagged,
    tag,
    content,
    tag_names,
};

/// The member pointer decides which spec type a component applies to, and
/// thereby whether it is valid in make_spec or make_struct_spec.
template <aspect A, auto Member, typename V>
struct value_component {
    V value;

    constexpr static aspect kind = A;

    template <typename Spec>
        requires requires(Spec& spec, const V& value) { spec.*Member = value; }
    constexpr void apply(Spec& spec) const {
        spec.*Member = value;
    }
};

/// `tagged = true` selects external tagging; internal and adjacent tagging are
/// selected by naming the tag (and content) fields instead.
struct tagged_component {
    bool value;

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
    constexpr static aspect kind = A;

    constexpr void apply(field_spec&) const {}
};

template <typename T>
constexpr bool is_type_component_v = false;

template <aspect A, typename T>
constexpr bool is_type_component_v<type_component<A, T>> = true;

/// A DSL component applicable to the given spec type.
template <typename T, typename Spec>
concept spec_component = requires(const T& component, Spec& spec) {
    { T::kind } -> std::convertible_to<aspect>;
    component.apply(spec);
};

template <aspect A, auto Member, typename V>
struct value_proxy {
    constexpr auto operator=(V value) const {
        return value_component<A, Member, V>{value};
    }
};

template <aspect A, auto Member>
struct name_list_proxy {
    constexpr auto operator=(std::initializer_list<std::string_view> names) const {
        if(names.size() > name_list::capacity) {
            KOTA_THROW("annotation: too many names");
        }
        value_component<A, Member, name_list> component{};
        for(auto name: names) {
            component.value.items[component.value.count++] = name;
        }
        return component;
    }
};

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

[[maybe_unused]] constexpr inline value_proxy<aspect::rename_all,
                                              &struct_spec::rename_all,
                                              naming::Casing> rename_all{};
[[maybe_unused]] constexpr inline value_proxy<aspect::deny_unknown_fields,
                                              &struct_spec::deny_unknown_fields,
                                              bool> deny_unknown_fields{};
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

/// What KOTATSU_ANNOTATE stores in its tag: the folded value spec, plus the
/// type components preserved in the result type.
template <typename... TypeComponents>
struct spec_result {
    field_spec value;

    using extras = std::tuple<TypeComponents...>;
};

namespace detail {

template <typename... Cs>
consteval bool component_kinds_unique() {
    std::array<dsl::aspect, sizeof...(Cs)> kinds = {Cs::kind...};
    for(std::size_t i = 0; i < kinds.size(); ++i) {
        for(std::size_t j = i + 1; j < kinds.size(); ++j) {
            if(kinds[i] == kinds[j]) {
                return false;
            }
        }
    }
    return true;
}

/// True for any DSL component, regardless of which spec type it applies to.
template <typename T>
concept any_component = dsl::spec_component<T, field_spec> || dsl::spec_component<T, struct_spec>;

constexpr void validate_spec(const field_spec& spec) {
    if(spec.skip && spec.flatten) {
        KOTA_THROW("annotation: skip and flatten conflict");
    }
    auto aliases = spec.alias.names();
    for(std::size_t i = 0; i < aliases.size(); ++i) {
        if(aliases[i].empty()) {
            KOTA_THROW("annotation: empty alias name");
        }
        if(aliases[i] == spec.rename) {
            KOTA_THROW("annotation: alias duplicates the rename name");
        }
        for(std::size_t j = i + 1; j < aliases.size(); ++j) {
            if(aliases[i] == aliases[j]) {
                KOTA_THROW("annotation: duplicate alias name");
            }
        }
    }
}

/// Derive the tagging mode from the tag/content field names and check the
/// combination for consistency.
constexpr struct_spec finalize_struct_spec(struct_spec spec) {
    if(spec.tagging == tag_mode::external && !spec.tag.empty()) {
        KOTA_THROW("annotation: tagged = true conflicts with a tag field name");
    }
    if(!spec.content.empty()) {
        if(spec.tag.empty()) {
            KOTA_THROW("annotation: content requires a tag field name");
        }
        if(spec.content == spec.tag) {
            KOTA_THROW("annotation: tag and content field names must differ");
        }
        spec.tagging = tag_mode::adjacent;
    } else if(!spec.tag.empty()) {
        spec.tagging = tag_mode::internal;
    }

    auto names = spec.tag_names.names();
    if(!names.empty() && spec.tagging == tag_mode::none) {
        KOTA_THROW("annotation: tag_names requires a tagging mode");
    }
    for(std::size_t i = 0; i < names.size(); ++i) {
        if(names[i].empty()) {
            KOTA_THROW("annotation: empty tag name");
        }
        for(std::size_t j = i + 1; j < names.size(); ++j) {
            if(names[i] == names[j]) {
                KOTA_THROW("annotation: duplicate tag name");
            }
        }
    }
    return spec;
}

}  // namespace detail

template <typename... Cs>
constexpr auto make_spec(const Cs&... components) {
    static_assert((detail::any_component<Cs> && ...),
                  "annotation entries must be assignments, e.g. skip = true");
    static_assert((dsl::spec_component<Cs, field_spec> && ...),
                  "struct-level entries (rename_all/deny_unknown_fields/tagged/...) cannot be "
                  "mixed with field-level entries in one annotation");
    static_assert(detail::component_kinds_unique<Cs...>(),
                  "annotation: the same attribute appears twice");

    field_spec spec{};
    (components.apply(spec), ...);
    detail::validate_spec(spec);

    using extras_t = decltype(std::tuple_cat(
        std::declval<
            std::conditional_t<dsl::is_type_component_v<Cs>, std::tuple<Cs>, std::tuple<>>>()...));
    return []<typename... Ts>(const field_spec& value, std::type_identity<std::tuple<Ts...>>) {
        return spec_result<Ts...>{value};
    }(spec, std::type_identity<extras_t>{});
}

template <typename... Cs>
constexpr struct_spec make_struct_spec(const Cs&... components) {
    static_assert((detail::any_component<Cs> && ...),
                  "annotation entries must be assignments, e.g. deny_unknown_fields = true");
    static_assert((dsl::spec_component<Cs, struct_spec> && ...),
                  "field-level entries are not valid in a struct annotation; use make_spec");
    static_assert(detail::component_kinds_unique<Cs...>(),
                  "annotation: the same attribute appears twice");

    struct_spec spec{};
    (components.apply(spec), ...);
    return detail::finalize_struct_spec(spec);
}

/// The entries decide what an annotation describes: struct-level entries only
/// make a struct_spec, anything else a field spec. KOTATSU_ANNOTATION routes
/// through this so one macro covers both.
template <typename... Cs>
constexpr auto make_annotation(const Cs&... components) {
    if constexpr(sizeof...(Cs) > 0 && (dsl::spec_component<Cs, struct_spec> && ...)) {
        return make_struct_spec(components...);
    } else {
        return make_spec(components...);
    }
}

}  // namespace kota::meta
