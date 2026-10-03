#pragma once

#include <concepts>
#include <cstddef>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "context.h"
#include "kota/support/type_list.h"
#include "kota/support/type_traits.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"

// Pieces the encode and decode dispatch share.

namespace kota::codec::detail {

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

/// Whether a node of type T can carry a tagging spec: only a std::variant
/// has alternatives to tag.
template <typename T>
concept taggable = is_specialization_of<std::variant, T>;

/// Whether a field's skip condition (a behavior::skip_if predicate or a
/// skip_when spec) holds for value, when encoding or when decoding.
template <typename Attrs, typename T>
bool skipped(const T& value, bool is_serialize) {
    if constexpr(tuple_has_spec_v<Attrs, meta::behavior::skip_if>) {
        using pred = typename tuple_find_spec_t<Attrs, meta::behavior::skip_if>::predicate;
        return meta::evaluate_skip_predicate<pred>(value, is_serialize);
    } else if constexpr(constexpr auto when = meta::spec_of<Attrs>.skip_if;
                        when != meta::skip_when::never) {
        return meta::evaluate_skip_when<when>(value, is_serialize);
    } else {
        return false;
    }
}

/// When a step fails, prepends where it was, a field name or an element
/// index, to the active error's path; returns ok. Config::detailed_error
/// turns the tracking off.
template <typename Config, typename Step>
bool trace_path(bool ok, const Step& at) {
    if constexpr(Config::detailed_error) {
        if(!ok) {
            if(auto* e = scoped_context<rich_error>::try_current()) {
                rich_error::prepend_segment(e->path, at);
            }
        }
    }
    return ok;
}

}  // namespace kota::codec::detail
