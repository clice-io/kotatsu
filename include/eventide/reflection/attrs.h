#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "struct.h"
#include "eventide/common/fixed_string.h"
#include "eventide/common/meta.h"
#include "eventide/common/tuple_traits.h"

namespace eventide::refl {

/// A hint attribute is transparent to the core serde framework.
/// Backends query hints by their own tag type and interpret them freely.
template <typename BackendTag, typename... KV>
struct hint {
    using backend_tag = BackendTag;
    using params = std::tuple<KV...>;
};

/// True for any hint<...> specialization.
template <typename T>
constexpr bool is_hint_attr_v = is_specialization_of<hint, T>;

namespace detail {

template <typename BackendTag, typename T>
constexpr bool hint_matches_v = false;

template <typename BackendTag, typename... KV>
constexpr bool hint_matches_v<BackendTag, hint<BackendTag, KV...>> = true;

template <typename BackendTag, typename... Attrs>
struct find_hint_impl {
    using type = void;
};

template <typename BackendTag, typename First, typename... Rest>
struct find_hint_impl<BackendTag, First, Rest...> {
    using type = std::conditional_t<hint_matches_v<BackendTag, First>,
                                    First,
                                    typename find_hint_impl<BackendTag, Rest...>::type>;
};

}  // namespace detail

/// Get the hint for a specific backend from an attrs tuple.
/// Returns void if no matching hint exists.
template <typename BackendTag, typename AttrsTuple>
struct get_hint;

template <typename BackendTag, typename... Attrs>
struct get_hint<BackendTag, std::tuple<Attrs...>> : detail::find_hint_impl<BackendTag, Attrs...> {};

template <typename BackendTag, typename AttrsTuple>
using get_hint_t = typename get_hint<BackendTag, AttrsTuple>::type;

template <typename T>
concept annotated_type = requires {
    typename std::remove_cvref_t<T>::annotated_type;
    typename std::remove_cvref_t<T>::attrs;
};

namespace attrs {

// Field-level
struct skip {};

struct flatten {};

/// Allow a field to be absent during deserialization.
/// When missing, the field keeps its default-constructed value.
/// Equivalent to Rust's #[serde(default)].
struct default_value {};

template <fixed_string Name>
struct rename {
    constexpr inline static std::string_view name = Name;
};

template <fixed_string... Names>
struct alias {
    constexpr inline static std::array names = {std::string_view(Names)...};
};

template <fixed_string Name>
struct literal {
    constexpr inline static std::string_view name = Name;
};

// Struct-level

/// Apply a rename policy to all fields of a struct.
template <typename Policy>
struct rename_all {
    using policy = Policy;
};

/// Reject unknown fields during deserialization.
struct deny_unknown_fields {};

/// Unified tagged variant representation.
/// - tagged<>             = externally tagged  (key is tag name, value is content)
/// - tagged<Tag>          = internally tagged  (tag field embedded in struct)
/// - tagged<Tag, Content> = adjacently tagged  (separate tag and content fields)
///
/// Use ::names<...> to provide custom tag names for each alternative.
/// Without ::names, defaults to refl::type_name<Alt>() for each alternative.
template <fixed_string... FieldNames>
struct tagged {
    static_assert(sizeof...(FieldNames) <= 2, "tagged: 0=external, 1=internal, 2=adjacent");

    constexpr static auto field_names =
        std::array<std::string_view, sizeof...(FieldNames)>{FieldNames...};
    constexpr static bool has_custom_names = false;

    template <fixed_string... Names>
    struct names {
        constexpr static auto field_names =
            std::array<std::string_view, sizeof...(FieldNames)>{FieldNames...};
        constexpr static auto tag_names = std::array<std::string_view, sizeof...(Names)>{Names...};
        constexpr static bool has_custom_names = true;
    };
};

/// Semantic aliases for backward compatibility.
using externally_tagged = tagged<>;

template <fixed_string Tag>
using internally_tagged = tagged<Tag>;

template <fixed_string Tag, fixed_string Content>
using adjacently_tagged = tagged<Tag, Content>;

}  // namespace attrs

template <typename T>
struct is_rename_attr {
    constexpr static bool value = false;
};

template <fixed_string N>
struct is_rename_attr<attrs::rename<N>> {
    constexpr static bool value = true;
};

template <typename T>
struct is_alias_attr {
    constexpr static bool value = false;
};

template <fixed_string... Ns>
struct is_alias_attr<attrs::alias<Ns...>> {
    constexpr static bool value = true;
};

template <typename T>
struct is_literal_attr {
    constexpr static bool value = false;
};

template <fixed_string N>
struct is_literal_attr<attrs::literal<N>> {
    constexpr static bool value = true;
};

/// Unified predicate for all tagged attrs (tagged<...> and tagged<...>::names<...>).
template <typename T>
struct is_tagged_attr {
    constexpr static bool value = requires {
        { T::field_names } -> std::convertible_to<std::span<const std::string_view>>;
        { T::has_custom_names } -> std::same_as<const bool&>;
    };
};

/// Strategy dispatch based on field_names count.
enum class tagged_strategy { external = 0, internal = 1, adjacent = 2 };

template <typename TagAttr>
constexpr tagged_strategy tagged_strategy_of =
    static_cast<tagged_strategy>(TagAttr::field_names.size());

/// Resolve tag names for variant alternatives.
/// With ::names, uses the user-provided names (static_assert on count match).
/// Without ::names, uses refl::type_name<Alt>() for each alternative.
template <typename TagAttr, typename... Ts>
constexpr auto resolve_tag_names() {
    if constexpr(TagAttr::has_custom_names) {
        static_assert(TagAttr::tag_names.size() == sizeof...(Ts),
                      "tagged: number of custom names must match variant alternatives");
        return TagAttr::tag_names;
    } else {
        return std::array<std::string_view, sizeof...(Ts)>{type_name<Ts>()...};
    }
}

/// True for the closed set of schema attributes (field-level + struct-level).
template <typename T>
constexpr bool is_schema_attr_v =
    std::is_same_v<T, attrs::skip> || std::is_same_v<T, attrs::flatten> ||
    std::is_same_v<T, attrs::default_value> || is_rename_attr<T>::value ||
    is_alias_attr<T>::value || is_literal_attr<T>::value ||
    is_specialization_of<attrs::rename_all, T> || std::is_same_v<T, attrs::deny_unknown_fields> ||
    is_tagged_attr<T>::value;

namespace attrs {

/// Get the canonical (wire) name for field I of struct T.
template <typename T, std::size_t I>
    requires reflectable_class<T>
consteval std::string_view canonical_field_name() {
    using field_t = field_type<T, I>;
    if constexpr(!annotated_type<field_t>) {
        return field_name<I, T>();
    } else {
        using attrs_t = typename field_t::attrs;
        if constexpr(tuple_any_of_v<attrs_t, is_rename_attr>) {
            return tuple_find_t<attrs_t, is_rename_attr>::name;
        } else {
            return field_name<I, T>();
        }
    }
}

/// True if field I is excluded from direct name matching (skip or flatten).
template <typename T, std::size_t I>
    requires reflectable_class<T>
consteval bool is_field_excluded() {
    using field_t = field_type<T, I>;
    if constexpr(!annotated_type<field_t>) {
        return false;
    } else {
        using attrs_t = typename field_t::attrs;
        return tuple_has_v<attrs_t, skip> || tuple_has_v<attrs_t, flatten>;
    }
}

/// True if field I is skipped.
template <typename T, std::size_t I>
    requires reflectable_class<T>
consteval bool is_field_skipped() {
    using field_t = field_type<T, I>;
    if constexpr(!annotated_type<field_t>) {
        return false;
    } else {
        return tuple_has_v<typename field_t::attrs, skip>;
    }
}

/// True if field I is flattened.
template <typename T, std::size_t I>
    requires reflectable_class<T>
consteval bool is_field_flattened() {
    using field_t = field_type<T, I>;
    if constexpr(!annotated_type<field_t>) {
        return false;
    } else {
        return tuple_has_v<typename field_t::attrs, flatten>;
    }
}

namespace detail {

template <typename T, std::size_t I>
consteval bool field_has_alias() {
    using field_t = field_type<T, I>;
    if constexpr(!annotated_type<field_t>) {
        return false;
    } else {
        return tuple_any_of_v<typename field_t::attrs, is_alias_attr>;
    }
}

template <typename T, std::size_t I>
consteval std::size_t alias_count() {
    if constexpr(!field_has_alias<T, I>()) {
        return 0;
    } else {
        using field_t = field_type<T, I>;
        using attrs_t = typename field_t::attrs;
        using alias_attr = tuple_find_t<attrs_t, is_alias_attr>;
        return alias_attr::names.size();
    }
}

}  // namespace detail

/// Validate that no two non-excluded fields share the same canonical name
/// and that aliases don't collide with canonical names.
template <typename T>
    requires reflectable_class<T>
consteval bool validate_field_schema() {
    constexpr std::size_t N = field_count<T>();

    return []<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        std::array<std::string_view, sizeof...(Is)> names = {canonical_field_name<T, Is>()...};
        std::array<bool, sizeof...(Is)> excluded = {is_field_excluded<T, Is>()...};

        // Check: no two non-excluded fields share the same canonical name
        for(std::size_t i = 0; i < sizeof...(Is); ++i) {
            if(excluded[i])
                continue;
            for(std::size_t j = i + 1; j < sizeof...(Is); ++j) {
                if(excluded[j])
                    continue;
                if(names[i] == names[j]) {
                    return false;
                }
            }
        }

        // Check: no alias collides with another field's canonical name
        auto verify_field_aliases = [&]<std::size_t I>() consteval {
            if(excluded[I])
                return true;
            if constexpr(!detail::field_has_alias<T, I>()) {
                return true;
            } else {
                using field_t = field_type<T, I>;
                using attrs_t = typename field_t::attrs;
                using alias_attr = tuple_find_t<attrs_t, is_alias_attr>;
                for(auto alias_name: alias_attr::names) {
                    for(std::size_t j = 0; j < sizeof...(Is); ++j) {
                        if(j == I || excluded[j])
                            continue;
                        if(alias_name == names[j]) {
                            return false;
                        }
                    }
                }
                return true;
            }
        };
        if(!(verify_field_aliases.template operator()<Is>() && ...)) {
            return false;
        }

        return true;
    }(std::make_index_sequence<N>{});
}

template <std::size_t AliasCount>
struct field_schema {
    std::string_view canonical_name;
    std::array<std::string_view, AliasCount> aliases;
    bool is_skipped = false;
    bool is_flattened = false;
};

/// Build a field_schema for field I of struct T.
template <typename T, std::size_t I>
    requires reflectable_class<T>
consteval auto resolve_field() {
    using field_t = field_type<T, I>;
    if constexpr(!annotated_type<field_t>) {
        return field_schema<0>{
            .canonical_name = field_name<I, T>(),
            .aliases = {},
            .is_skipped = false,
            .is_flattened = false,
        };
    } else {
        using attrs_t = typename field_t::attrs;
        constexpr std::size_t alias_n = detail::alias_count<T, I>();

        auto get_aliases = []() consteval {
            if constexpr(alias_n == 0) {
                return std::array<std::string_view, 0>{};
            } else {
                using alias_attr = tuple_find_t<attrs_t, is_alias_attr>;
                return alias_attr::names;
            }
        };

        return field_schema<alias_n>{
            .canonical_name = canonical_field_name<T, I>(),
            .aliases = get_aliases(),
            .is_skipped = is_field_skipped<T, I>(),
            .is_flattened = is_field_flattened<T, I>(),
        };
    }
}

/// Build the complete field schema tuple for struct T.
template <typename T>
    requires reflectable_class<T>
consteval auto effective_field_schema() {
    return []<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        return std::make_tuple(resolve_field<T, Is>()...);
    }(std::make_index_sequence<field_count<T>()>{});
}

}  // namespace attrs

namespace behavior {

template <typename Policy>
struct enum_string {
    using policy = Policy;
};

template <typename Pred>
struct skip_if {
    using predicate = Pred;
};

/// Adapter-based serialization: the adapter fully controls value (de)serialization.
/// Protocol: Adapter::serialize(S&, const T&) and/or Adapter::deserialize(D&, T&)
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
        return static_cast<bool>(Pred{}(value));
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
        if constexpr(requires {
                         T{};
                         value == T{};
                     }) {
            return is_serialize && static_cast<bool>(value == T{});
        } else {
            return false;
        }
    }
};

}  // namespace pred

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

template <typename AttrsTuple>
constexpr bool validate_attrs() {
    static_assert(tuple_count_of_v<AttrsTuple, is_behavior_provider> <= 1,
                  "At most one behavior provider (with/as/enum_string) allowed per field");
    return true;
}

}  // namespace detail

}  // namespace eventide::refl
