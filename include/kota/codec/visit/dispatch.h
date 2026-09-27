#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

#include "context.h"
#include "kota/support/type_list.h"
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

    static type& of(T& object) {
        auto* base = reinterpret_cast<std::byte*>(std::addressof(object));
        return *reinterpret_cast<type*>(base + schema::fields[I].offset);
    }

    const static type& of(const T& object) {
        const auto* base = reinterpret_cast<const std::byte*>(std::addressof(object));
        return *reinterpret_cast<const type*>(base + schema::fields[I].offset);
    }
};

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

/// When a step fails, prepends where it was (a field name or an element
/// index) to the active error's path; returns ok. Config::detailed_error
/// turns the tracking off.
template <typename Config>
bool trace_path(bool ok, std::string_view field) {
    if constexpr(Config::detailed_error) {
        if(!ok) {
            if(auto* e = scoped_context<rich_error>::try_current()) {
                e->prepend_field(field);
            }
        }
    }
    return ok;
}

template <typename Config>
bool trace_path(bool ok, std::size_t index) {
    if constexpr(Config::detailed_error) {
        if(!ok) {
            if(auto* e = scoped_context<rich_error>::try_current()) {
                e->prepend_index(index);
            }
        }
    }
    return ok;
}

/// Calls f with std::integral_constant<std::size_t, I> for the I equal to
/// the runtime index, which must be below N, and returns f's result.
template <std::size_t N, typename F>
bool with_index(std::size_t index, F&& f) {
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        bool result = false;
        (void)((Is == index && ((result = f(std::integral_constant<std::size_t, Is>{})), true)) ||
               ...);
        return result;
    }(std::make_index_sequence<N>{});
}

}  // namespace kota::codec::detail
