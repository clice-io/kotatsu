#pragma once

#include <cstddef>
#include <expected>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/support/expected_try.h"
#include "kota/support/type_list.h"
#include "kota/support/type_traits.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/codec/backend.h"
#include "kota/codec/config.h"
#include "kota/codec/detail/apply_behavior.h"
#include "kota/codec/detail/fwd.h"

namespace kota::codec::detail {

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_externally_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E>;

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_internally_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E>;

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_adjacently_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E>;

template <typename S, typename E>
auto emit_field_value(S& s, std::expected<typename S::value_type, E>&& r)
    -> std::expected<void, E> {
    if(!r) {
        return std::unexpected(std::move(r).error());
    }
    if constexpr(!std::is_void_v<typename S::value_type>) {
        if constexpr(requires { s.accept_field_value(std::move(*r)); }) {
            return s.accept_field_value(std::move(*r));
        }
    }
    return {};
}

template <typename S, typename E>
auto emit_element_value(S& s, std::expected<typename S::value_type, E>&& r)
    -> std::expected<void, E> {
    if(!r) {
        return std::unexpected(std::move(r).error());
    }
    if constexpr(!std::is_void_v<typename S::value_type>) {
        if constexpr(requires { s.accept_element_value(std::move(*r)); }) {
            return s.accept_element_value(std::move(*r));
        }
    }
    return {};
}

template <typename Attrs, typename E, typename S, typename V>
auto serialize_slot_value(S& s, const V& value) -> std::expected<void, E> {
    if constexpr(tuple_count_of_v<Attrs, meta::is_behavior_provider> > 0) {
        auto result = apply_serialize_behavior<Attrs, V, E>(
            value,
            [&](const auto& v) -> std::expected<void, E> {
                return emit_field_value<S, E>(s, codec::serialize(s, v));
            },
            [&](auto tag, const auto& v) -> std::expected<void, E> {
                using Adapter = typename decltype(tag)::type;
                if constexpr(requires { Adapter::to_wire(v); }) {
                    auto wire = Adapter::to_wire(v);
                    return emit_field_value<S, E>(s, codec::serialize(s, wire));
                } else {
                    return emit_field_value<S, E>(s, codec::serialize(s, v));
                }
            });
        if(result.has_value()) {
            return *result;
        }
    }

    if constexpr(is_specialization_of<std::variant, std::remove_cvref_t<V>> &&
                 tuple_any_of_v<Attrs, meta::is_tagged_attr>) {
        using tag_attr = tuple_find_t<Attrs, meta::is_tagged_attr>;
        constexpr auto strategy = meta::tagged_strategy_of<tag_attr>;
        if constexpr(strategy == meta::tagged_strategy::external) {
            return emit_field_value<S, E>(s, serialize_externally_tagged<E>(s, value, tag_attr{}));
        } else if constexpr(strategy == meta::tagged_strategy::internal) {
            return emit_field_value<S, E>(s, serialize_internally_tagged<E>(s, value, tag_attr{}));
        } else {
            return emit_field_value<S, E>(s, serialize_adjacently_tagged<E>(s, value, tag_attr{}));
        }
    } else {
        return emit_field_value<S, E>(s, codec::serialize(s, value));
    }
}

template <typename Config, typename E, typename T, std::size_t I, typename S>
auto serialize_slot_by_name(S& s, const T& v) -> std::expected<void, E> {
    using schema = meta::virtual_schema<T, Config>;
    using slot_t = type_list_element_t<I, typename schema::slots>;
    using raw_t = std::remove_cv_t<typename slot_t::raw_type>;
    using attrs_t = typename slot_t::attrs;

    constexpr std::size_t offset = schema::fields[I].offset;
    const auto* base = reinterpret_cast<const std::byte*>(std::addressof(v));
    const auto& field_value = *reinterpret_cast<const raw_t*>(base + offset);

    if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::skip_if>) {
        using pred = typename tuple_find_spec_t<attrs_t, meta::behavior::skip_if>::predicate;
        if(meta::evaluate_skip_predicate<pred>(field_value, /*is_serialize=*/true)) {
            return {};
        }
    }

    std::string_view name = schema::fields[I].name;
    KOTA_EXPECTED_TRY(s.field(name));
    return serialize_slot_value<attrs_t, E>(s, field_value);
}

template <typename Config, typename E, typename T, std::size_t I, typename S>
auto serialize_slot_by_position(S& s, const T& v) -> std::expected<void, E> {
    using schema = meta::virtual_schema<T, Config>;
    using slot_t = type_list_element_t<I, typename schema::slots>;
    using raw_t = std::remove_cv_t<typename slot_t::raw_type>;
    using attrs_t = typename slot_t::attrs;

    constexpr std::size_t offset = schema::fields[I].offset;
    const auto* base = reinterpret_cast<const std::byte*>(std::addressof(v));
    const auto& field_value = *reinterpret_cast<const raw_t*>(base + offset);

    if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::skip_if>) {
        using pred = typename tuple_find_spec_t<attrs_t, meta::behavior::skip_if>::predicate;
        if(meta::evaluate_skip_predicate<pred>(field_value, /*is_serialize=*/true)) {
            return {};
        }
    }

    return serialize_slot_value<attrs_t, E>(s, field_value);
}

template <typename Config, typename E, typename S, typename T>
auto struct_serialize_by_name(S& s, const T& v) -> std::expected<typename S::value_type, E> {
    using schema = meta::virtual_schema<T, Config>;
    using slots = typename schema::slots;
    constexpr std::size_t N = type_list_size_v<slots>;

    KOTA_EXPECTED_TRY(s.begin_object(N));

    std::expected<void, E> status{};
    bool ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return ([&] {
            auto r = serialize_slot_by_name<Config, E, T, Is>(s, v);
            if(!r) {
                status = std::unexpected(r.error());
                return false;
            }
            return true;
        }() && ...);
    }(std::make_index_sequence<N>{});

    if(!ok) {
        return std::unexpected(status.error());
    }
    return s.end_object();
}

template <typename Config, typename E, typename S, typename T>
auto struct_serialize_by_position(S& s, const T& v) -> std::expected<typename S::value_type, E> {
    using schema = meta::virtual_schema<T, Config>;
    using slots = typename schema::slots;
    constexpr std::size_t N = type_list_size_v<slots>;

    std::expected<void, E> status{};
    bool ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return ([&] {
            auto r = serialize_slot_by_position<Config, E, T, Is>(s, v);
            if(!r) {
                status = std::unexpected(r.error());
                return false;
            }
            return true;
        }() && ...);
    }(std::make_index_sequence<N>{});

    if(!ok) {
        return std::unexpected(status.error());
    }
    return {};
}

template <typename Config, typename E, typename S, typename T>
auto struct_serialize(S& s, const T& v) -> std::expected<typename S::value_type, E> {
    if constexpr(S::field_mode_v == field_mode::by_name) {
        return struct_serialize_by_name<Config, E>(s, v);
    } else if constexpr(S::field_mode_v == field_mode::by_position) {
        return struct_serialize_by_position<Config, E>(s, v);
    } else {
        static_assert(sizeof(S) == 0, "by_tag not yet implemented");
    }
}

}  // namespace kota::codec::detail
