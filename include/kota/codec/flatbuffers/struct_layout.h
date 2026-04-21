#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "kota/codec/codec.h"
#include "kota/codec/detail/common.h"

namespace kota::codec::flatbuffers {

namespace schema_detail {

using codec::detail::remove_annotation_t;
using codec::detail::remove_optional_t;
using codec::detail::clean_t;

template <typename T>
constexpr bool is_scalar_field_v =
    std::same_as<T, bool> || codec::int_like<T> || codec::uint_like<T> || codec::floating_like<T>;

template <typename T>
struct schema_struct_trait;

template <typename T>
constexpr bool is_schema_struct_field_v = [] {
    using U = remove_optional_t<T>;
    if constexpr(is_scalar_field_v<U> || std::is_enum_v<U>) {
        return true;
    } else if constexpr(meta::reflectable_class<U>) {
        return schema_struct_trait<U>::value;
    } else {
        return false;
    }
}();

template <typename T>
struct schema_struct_trait {
    static consteval bool fields_supported() {
        if constexpr(!meta::reflectable_class<T>) {
            return false;
        } else {
            return []<std::size_t... I>(std::index_sequence<I...>) {
                return (is_schema_struct_field_v<meta::field_type<T, I>> && ...);
            }(std::make_index_sequence<meta::field_count<T>()>{});
        }
    }

    constexpr static bool value = meta::reflectable_class<T> && std::is_trivial_v<T> &&
                                  std::is_standard_layout_v<T> && fields_supported();
};

template <typename T>
constexpr bool is_schema_struct_v = schema_struct_trait<T>::value;

}  // namespace schema_detail

template <typename T>
constexpr bool is_schema_struct_v = schema_detail::is_schema_struct_v<T>;

template <typename T>
consteval bool has_annotated_fields() {
    using U = std::remove_cvref_t<T>;
    if constexpr(!meta::reflectable_class<U>) {
        return false;
    } else {
        return []<std::size_t... I>(std::index_sequence<I...>) {
            return (meta::annotated_type<meta::field_type<U, I>> || ...);
        }(std::make_index_sequence<meta::field_count<U>()>{});
    }
}

template <typename T>
constexpr bool can_inline_struct_v =
    meta::reflectable_class<T> && is_schema_struct_v<T> && !has_annotated_fields<T>();

}  // namespace kota::codec::flatbuffers
