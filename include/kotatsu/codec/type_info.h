#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "context.h"
#include "kotatsu/support/type_list.h"
#include "kotatsu/support/type_traits.h"
#include "kotatsu/meta/attrs.h"
#include "kotatsu/meta/compare.h"
#include "kotatsu/meta/schema.h"

// Pieces the encode and decode dispatch share.

namespace kotatsu::codec::detail {

/// Struct field I of T as the dispatch sees it under Config: the slot's
/// type and attrs, its serialized name, and the field inside an object.
template <typename Config, std::size_t I, typename T>
struct FieldAt {
    using schema = meta::virtual_schema<T, Config>;
    using slot = type_list_element_t<I, typename schema::slots>;
    using type = std::remove_cv_t<typename slot::raw_type>;
    using attrs = typename slot::attrs;

    constexpr static std::string_view name = schema::fields[I].name;

    /// The field inside object, as const as object is.
    template <typename Object>
        requires std::same_as<std::remove_const_t<Object>, T>
    static auto& of(Object& object) {
        constexpr bool is_const = std::is_const_v<Object>;
        using byte_t = std::conditional_t<is_const, const std::byte, std::byte>;
        using field_t = std::conditional_t<is_const, const type, type>;
        auto* base = reinterpret_cast<byte_t*>(std::addressof(object));
        return *reinterpret_cast<field_t*>(base + schema::fields[I].offset);
    }
};

/// Whether a field of struct T under Config answers to name, by its own name
/// or an alias.
template <typename Config, typename T>
consteval bool has_field_named(std::string_view name) {
    return std::ranges::any_of(meta::virtual_schema<T, Config>::fields,
                               [&](const meta::field_info& field) {
                                   return field.name == name ||
                                          std::ranges::find(field.aliases, name) !=
                                              field.aliases.end();
                               });
}

/// Rejects an internally tagged variant one of whose alternatives has a
/// field named like the tag: its document would hold the key twice.
template <typename Config, typename SpecAttr, typename... Ts>
consteval void assert_internal_tag_fits() {
    static_assert((!has_field_named<Config, Ts>(SpecAttr::value.tag) && ...),
                  "internally tagged: an alternative has a field named like the tag, so its "
                  "document would hold the key twice");
}

/// Whether a node of type T can carry a tagging spec: only a std::variant
/// has alternatives to tag.
template <typename T>
concept taggable = is_specialization_of<std::variant, T>;

/// Whether a write leaves out field I of owner: it is its default, the field
/// of a value-initialized owner, under presence = omit_if_default, or its
/// predicate holds for it under presence = type<Pred>.
template <typename Config, std::size_t I, typename T>
bool omitted(const T& owner) {
    using field = FieldAt<Config, I, T>;
    constexpr auto presence = field::schema::fields[I].presence;
    if constexpr(presence == meta::presence_kind::omit_if_default) {
        static_assert(std::is_default_constructible_v<T>,
                      "presence = omit_if_default compares a field with its value in a "
                      "value-initialized struct: the struct needs a default constructor");
        const static T defaults = T();
        return meta::eq(field::of(owner), field::of(defaults));
    } else if constexpr(presence == meta::presence_kind::omit_if) {
        using pred =
            typename tuple_find_spec_t<typename field::attrs, meta::behavior::omit_if>::predicate;
        return static_cast<bool>(pred{}(field::of(owner)));
    } else {
        return false;
    }
}

}  // namespace kotatsu::codec::detail
