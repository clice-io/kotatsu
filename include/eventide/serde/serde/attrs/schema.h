#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "eventide/common/fixed_string.h"
#include "eventide/common/meta.h"
#include "eventide/reflection/struct.h"

namespace eventide::serde {

namespace detail {

/// True if any type in Ts... satisfies Pred.
template <template <typename> class Pred, typename... Ts>
constexpr bool any_attr_match_v = (Pred<Ts>::value || ...);

template <template <typename> class Pred>
constexpr bool any_attr_match_v<Pred> = false;

/// True if any element in Tuple satisfies Pred (for struct predicates).
template <typename Tuple, template <typename> class Pred>
constexpr bool tuple_any_of_v = false;

template <template <typename> class Pred, typename... Attrs>
constexpr bool tuple_any_of_v<std::tuple<Attrs...>, Pred> = any_attr_match_v<Pred, Attrs...>;

/// True if Tuple contains an exact match for Tag.
template <typename Tuple, typename Tag>
constexpr bool tuple_has_v = false;

template <typename Tag, typename... Attrs>
constexpr bool tuple_has_v<std::tuple<Attrs...>, Tag> = (std::is_same_v<Attrs, Tag> || ...);

/// True if Tuple contains a specialization of HKT.
template <typename Tuple, template <typename...> typename HKT>
constexpr bool tuple_has_spec_v = false;

template <template <typename...> typename HKT, typename... Attrs>
constexpr bool tuple_has_spec_v<std::tuple<Attrs...>, HKT> =
    (is_specialization_of<HKT, Attrs> || ...);

/// Count of elements in Ts... that satisfy Pred.
template <template <typename> class Pred, typename... Ts>
constexpr std::size_t count_attr_match_v = (static_cast<std::size_t>(Pred<Ts>::value) + ...);

template <template <typename> class Pred>
constexpr std::size_t count_attr_match_v<Pred> = 0;

/// Count of elements in Tuple that satisfy Pred (for struct predicates).
template <typename Tuple, template <typename> class Pred>
constexpr std::size_t tuple_count_of_v = 0;

template <template <typename> class Pred, typename... Attrs>
constexpr std::size_t tuple_count_of_v<std::tuple<Attrs...>, Pred> =
    count_attr_match_v<Pred, Attrs...>;

/// Find the first type in a pack that satisfies Pred. Returns void if none.
template <template <typename> class Pred, typename... Ts>
struct find_first_impl {
    using type = void;
};

template <template <typename> class Pred, typename First, typename... Rest>
struct find_first_impl<Pred, First, Rest...> {
    using type = std::
        conditional_t<Pred<First>::value, First, typename find_first_impl<Pred, Rest...>::type>;
};

/// Find the first element in Tuple that satisfies Pred. Returns void if none.
template <typename Tuple, template <typename> class Pred>
struct tuple_find;

template <template <typename> class Pred, typename... Attrs>
struct tuple_find<std::tuple<Attrs...>, Pred> : find_first_impl<Pred, Attrs...> {};

template <typename Tuple, template <typename> class Pred>
using tuple_find_t = typename tuple_find<Tuple, Pred>::type;

/// Find the first specialization of HKT in a pack. Returns void if none.
template <template <typename...> typename HKT, typename... Ts>
struct find_first_spec_impl {
    using type = void;
};

template <template <typename...> typename HKT, typename First, typename... Rest>
struct find_first_spec_impl<HKT, First, Rest...> {
    using type = std::conditional_t<is_specialization_of<HKT, First>,
                                    First,
                                    typename find_first_spec_impl<HKT, Rest...>::type>;
};

/// Find the first specialization of HKT in Tuple. Returns void if none.
template <typename Tuple, template <typename...> typename HKT>
struct tuple_find_spec;

template <template <typename...> typename HKT, typename... Attrs>
struct tuple_find_spec<std::tuple<Attrs...>, HKT> : find_first_spec_impl<HKT, Attrs...> {};

template <typename Tuple, template <typename...> typename HKT>
using tuple_find_spec_t = typename tuple_find_spec<Tuple, HKT>::type;

}  // namespace detail

namespace schema {

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

}  // namespace schema

template <typename T>
struct is_rename_attr {
    constexpr static bool value = false;
};

template <fixed_string N>
struct is_rename_attr<schema::rename<N>> {
    constexpr static bool value = true;
};

template <typename T>
struct is_alias_attr {
    constexpr static bool value = false;
};

template <fixed_string... Ns>
struct is_alias_attr<schema::alias<Ns...>> {
    constexpr static bool value = true;
};

template <typename T>
struct is_literal_attr {
    constexpr static bool value = false;
};

template <fixed_string N>
struct is_literal_attr<schema::literal<N>> {
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
        return std::array<std::string_view, sizeof...(Ts)>{refl::type_name<Ts>()...};
    }
}

/// True for the closed set of schema attributes (field-level + struct-level).
template <typename T>
constexpr bool is_schema_attr_v =
    std::is_same_v<T, schema::skip> || std::is_same_v<T, schema::flatten> ||
    std::is_same_v<T, schema::default_value> || is_rename_attr<T>::value ||
    is_alias_attr<T>::value || is_literal_attr<T>::value ||
    is_specialization_of<schema::rename_all, T> || std::is_same_v<T, schema::deny_unknown_fields> ||
    is_tagged_attr<T>::value;

template <typename T>
concept has_attrs = requires {
    typename std::remove_cvref_t<T>::annotated_type;
    typename std::remove_cvref_t<T>::attrs;
};

namespace schema {

/// Get the canonical (wire) name for field I of struct T.
template <typename T, std::size_t I>
    requires refl::reflectable_class<T>
consteval std::string_view canonical_field_name() {
    using field_t = refl::field_type<T, I>;
    if constexpr(!serde::has_attrs<field_t>) {
        return refl::field_name<I, T>();
    } else {
        using attrs_t = typename field_t::attrs;
        if constexpr(serde::detail::tuple_any_of_v<attrs_t, is_rename_attr>) {
            return serde::detail::tuple_find_t<attrs_t, is_rename_attr>::name;
        } else {
            return refl::field_name<I, T>();
        }
    }
}

/// True if field I is excluded from direct name matching (skip or flatten).
template <typename T, std::size_t I>
    requires refl::reflectable_class<T>
consteval bool is_field_excluded() {
    using field_t = refl::field_type<T, I>;
    if constexpr(!serde::has_attrs<field_t>) {
        return false;
    } else {
        using attrs_t = typename field_t::attrs;
        return serde::detail::tuple_has_v<attrs_t, skip> ||
               serde::detail::tuple_has_v<attrs_t, flatten>;
    }
}

/// True if field I is skipped.
template <typename T, std::size_t I>
    requires refl::reflectable_class<T>
consteval bool is_field_skipped() {
    using field_t = refl::field_type<T, I>;
    if constexpr(!serde::has_attrs<field_t>) {
        return false;
    } else {
        return serde::detail::tuple_has_v<typename field_t::attrs, skip>;
    }
}

/// True if field I is flattened.
template <typename T, std::size_t I>
    requires refl::reflectable_class<T>
consteval bool is_field_flattened() {
    using field_t = refl::field_type<T, I>;
    if constexpr(!serde::has_attrs<field_t>) {
        return false;
    } else {
        return serde::detail::tuple_has_v<typename field_t::attrs, flatten>;
    }
}

namespace detail {

template <typename T, std::size_t I>
consteval bool field_has_alias() {
    using field_t = refl::field_type<T, I>;
    if constexpr(!serde::has_attrs<field_t>) {
        return false;
    } else {
        return serde::detail::tuple_any_of_v<typename field_t::attrs, is_alias_attr>;
    }
}

template <typename T, std::size_t I>
consteval std::size_t alias_count() {
    if constexpr(!field_has_alias<T, I>()) {
        return 0;
    } else {
        using field_t = refl::field_type<T, I>;
        using attrs_t = typename field_t::attrs;
        using alias_attr = serde::detail::tuple_find_t<attrs_t, is_alias_attr>;
        return alias_attr::names.size();
    }
}

}  // namespace detail

/// Validate that no two non-excluded fields share the same canonical name
/// and that aliases don't collide with canonical names.
template <typename T>
    requires refl::reflectable_class<T>
consteval bool validate_field_schema() {
    constexpr std::size_t N = refl::field_count<T>();

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
                using field_t = refl::field_type<T, I>;
                using attrs_t = typename field_t::attrs;
                using alias_attr = serde::detail::tuple_find_t<attrs_t, is_alias_attr>;
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
    requires refl::reflectable_class<T>
consteval auto resolve_field() {
    using field_t = refl::field_type<T, I>;
    if constexpr(!serde::has_attrs<field_t>) {
        return field_schema<0>{
            .canonical_name = refl::field_name<I, T>(),
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
                using alias_attr = serde::detail::tuple_find_t<attrs_t, is_alias_attr>;
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
    requires refl::reflectable_class<T>
consteval auto effective_field_schema() {
    return []<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        return std::make_tuple(resolve_field<T, Is>()...);
    }(std::make_index_sequence<refl::field_count<T>()>{});
}

}  // namespace schema

}  // namespace eventide::serde
