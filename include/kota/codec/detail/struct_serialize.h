#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/support/expected_try.h"
#include "kota/support/type_list.h"
#include "kota/support/type_traits.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/codec/detail/apply_behavior.h"
#include "kota/codec/detail/backend.h"
#include "kota/codec/detail/config.h"

namespace kota::codec::detail {

template <typename Config, bool IsSerialize, typename T, typename Visitor>
auto for_each_field(T&& value, Visitor&& visitor)
    -> std::expected<void, typename std::remove_cvref_t<Visitor>::error_type> {
    using clean_t = std::remove_cvref_t<T>;
    using schema = meta::virtual_schema<clean_t, Config>;
    using slots = typename schema::slots;
    constexpr std::size_t N = kota::type_list_size_v<slots>;
    using E = typename std::remove_cvref_t<Visitor>::error_type;

    // Determine pointer type based on const-ness of T
    constexpr bool is_const = std::is_const_v<std::remove_reference_t<T>>;

    std::expected<void, E> status{};
    bool ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return ([&] {
            using slot_t = kota::type_list_element_t<Is, slots>;
            using raw_t = std::remove_cv_t<typename slot_t::raw_type>;
            using attrs_t = typename slot_t::attrs;

            constexpr std::size_t offset = schema::fields[Is].offset;

            // Get field reference with appropriate const-ness
            decltype(auto) field_ref = [&]() -> decltype(auto) {
                if constexpr(is_const) {
                    const auto* base = reinterpret_cast<const std::byte*>(std::addressof(value));
                    return *reinterpret_cast<const raw_t*>(base + offset);
                } else {
                    auto* base = reinterpret_cast<std::byte*>(std::addressof(value));
                    return *reinterpret_cast<raw_t*>(base + offset);
                }
            }();

            // Check skip_if predicate
            if constexpr(kota::tuple_has_spec_v<attrs_t, meta::behavior::skip_if>) {
                using pred =
                    typename kota::tuple_find_spec_t<attrs_t, meta::behavior::skip_if>::predicate;
                if(meta::evaluate_skip_predicate<pred>(field_ref, IsSerialize)) {
                    auto r = visitor.template on_skip<Is, raw_t, attrs_t>(field_ref);
                    if(!r) {
                        status = std::unexpected(r.error());
                        return false;
                    }
                    return true;
                }
            }

            auto r =
                visitor.template on_field<Is, raw_t, attrs_t>(field_ref, schema::fields[Is].name);
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

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_externally_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E>;

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_internally_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E>;

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_adjacently_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E>;

template <typename Attrs, typename E, typename S, typename V>
auto serialize_slot_value(S& s, const V& value) -> std::expected<typename S::value_type, E> {
    using VT = typename S::value_type;
    if constexpr(tuple_count_of_v<Attrs, meta::is_behavior_provider> > 0) {
        auto result = apply_serialize_behavior<Attrs, V, E>(
            value,
            [&](const auto& v) -> std::expected<VT, E> { return codec::serialize(s, v); },
            [&](auto tag, const auto& v) -> std::expected<VT, E> {
                using Adapter = typename decltype(tag)::type;
                if constexpr(requires { Adapter::to_wire(v); }) {
                    auto wire = Adapter::to_wire(v);
                    return codec::serialize(s, wire);
                } else if constexpr(requires { Adapter::serialize(s, v); }) {
                    return Adapter::serialize(s, v);
                } else {
                    return codec::serialize(s, v);
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
            return serialize_externally_tagged<E>(s, value, tag_attr{});
        } else if constexpr(strategy == meta::tagged_strategy::internal) {
            return serialize_internally_tagged<E>(s, value, tag_attr{});
        } else {
            return serialize_adjacently_tagged<E>(s, value, tag_attr{});
        }
    } else {
        return codec::serialize(s, value);
    }
}

template <typename E, typename S>
struct serialize_by_name_visitor {
    using error_type = E;
    S& s;

    template <std::size_t I, typename raw_t, typename attrs_t>
    auto on_field(const raw_t& field_ref, std::string_view name) -> std::expected<void, E> {
        return s.serialize_field(name,
                                 [&] { return serialize_slot_value<attrs_t, E>(s, field_ref); });
    }

    template <std::size_t I, typename raw_t, typename attrs_t>
    auto on_skip(const raw_t&) -> std::expected<void, E> {
        return {};
    }
};

template <typename E, typename S>
struct serialize_by_position_visitor {
    using error_type = E;
    S& s;

    template <std::size_t I, typename raw_t, typename attrs_t>
    auto on_field(const raw_t& field_ref, std::string_view) -> std::expected<void, E> {
        return serialize_slot_value<attrs_t, E>(s, field_ref);
    }

    template <std::size_t I, typename raw_t, typename attrs_t>
    auto on_skip(const raw_t&) -> std::expected<void, E> {
        // For by-position formats, we cannot omit the slot — it would shift
        // all subsequent field positions.  Serialize the default value instead.
        raw_t default_value{};
        return serialize_slot_value<attrs_t, E>(s, default_value);
    }
};

template <typename Config, typename E, typename S, typename T>
auto struct_serialize_by_name(S& s, const T& v) -> std::expected<typename S::value_type, E> {
    using schema = meta::virtual_schema<T, Config>;
    constexpr std::size_t N = type_list_size_v<typename schema::slots>;

    KOTA_EXPECTED_TRY(s.begin_object(N));
    serialize_by_name_visitor<E, S> visitor{s};
    KOTA_EXPECTED_TRY((for_each_field<Config, true>(v, visitor)));
    return s.end_object();
}

template <typename Config, typename E, typename S, typename T>
auto struct_serialize_by_position(S& s, const T& v) -> std::expected<typename S::value_type, E> {
    static_assert(std::is_void_v<typename S::value_type>,
                  "by_position serialization requires value_type = void");

    serialize_by_position_visitor<E, S> visitor{s};
    KOTA_EXPECTED_TRY((for_each_field<Config, true>(v, visitor)));
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
