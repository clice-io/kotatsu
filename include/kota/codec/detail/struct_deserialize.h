#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
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
#include "kota/codec/detail/reflectable.h"

namespace kota::codec::detail {

/// Deserialize a single slot's value after applying reverse behavior attributes.
template <typename Attrs, typename E, typename D, typename V>
auto deserialize_slot_value(D& d, V& value) -> std::expected<void, E> {
    if constexpr(tuple_count_of_v<Attrs, meta::is_behavior_provider> > 0) {
        auto result = apply_deserialize_behavior<Attrs, V, E>(
            value,
            [&](auto& v) { return codec::deserialize(d, v); },
            [&](auto tag, auto& v) -> std::expected<void, E> {
                using Adapter = typename decltype(tag)::type;
                if constexpr(requires { Adapter::from_wire(std::declval<typename Adapter::wire_type>()); }) {
                    using wire_t = typename Adapter::wire_type;
                    wire_t wire{};
                    KOTA_EXPECTED_TRY(codec::deserialize(d, wire));
                    v = Adapter::from_wire(std::move(wire));
                    return {};
                } else {
                    return codec::deserialize(d, v);
                }
            });
        if(result.has_value()) {
            return *result;
        }
    }

    // No behavior or tagged variant passthrough
    return codec::deserialize(d, value);
}

/// Dispatch deserialization to the correct slot by runtime index (by_name mode).
/// Uses virtual_schema offsets for direct field access.
template <typename T, typename Config, typename E, typename D>
auto dispatch_slot_deserialize(D& d, std::size_t slot_index, T& value) -> std::expected<void, E> {
    using schema = meta::virtual_schema<T, Config>;
    using slots = typename schema::slots;
    constexpr std::size_t N = type_list_size_v<slots>;

    return [&]<std::size_t... Is>(std::index_sequence<Is...>) -> std::expected<void, E> {
        std::expected<void, E> result;
        bool matched = false;
        (([&] {
             if(matched || Is != slot_index) {
                 return;
             }
             matched = true;

             using slot_t = type_list_element_t<Is, slots>;
             using raw_t = std::remove_cv_t<typename slot_t::raw_type>;
             using attrs_t = typename slot_t::attrs;

             constexpr std::size_t offset = schema::fields[Is].offset;
             auto* base = reinterpret_cast<std::byte*>(std::addressof(value));
             auto& field_value = *reinterpret_cast<raw_t*>(base + offset);

             // skip_if
             if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::skip_if>) {
                 using pred =
                     typename tuple_find_spec_t<attrs_t, meta::behavior::skip_if>::predicate;
                 if(meta::evaluate_skip_predicate<pred>(field_value, false)) {
                     // For deserialization, skip_if means: skip reading the value from wire
                     // but we still need to consume the wire token. Use the deserializer's skip.
                     if constexpr(requires { d.skip_field_value(); }) {
                         result = d.skip_field_value();
                     }
                     return;
                 }
             }

             result = deserialize_slot_value<attrs_t, E>(d, field_value);
         }()),
         ...);

        if(!matched) {
            return std::unexpected(E::type_mismatch);
        }
        return result;
    }(std::make_index_sequence<N>{});
}

/// Virtual-schema-driven struct deserialization (by_name mode).
/// Requires the deserializer to provide:
///   d.begin_object()              → status_t
///   d.next_field()                → result_t<optional<string_view>>
///   d.deserialize_field_value(v)  → status_t  (or the top-level deserialize works)
///   d.skip_field_value()          → status_t
///   d.end_object()                → status_t
template <typename Config, typename E, typename D, typename T>
auto struct_deserialize_by_name(D& d, T& v) -> std::expected<void, E> {
    using schema = meta::virtual_schema<T, Config>;

    // Reuse existing lookup infrastructure from reflectable.h
    if(has_ambiguous_wire_names<T, Config>()) {
        return std::unexpected(E::invalid_state);
    }

    KOTA_EXPECTED_TRY(d.begin_object());

    std::uint64_t seen_fields = 0;

    while(true) {
        KOTA_EXPECTED_TRY_V(auto key, d.next_field());
        if(!key.has_value()) {
            break;
        }

        std::string_view key_name = *key;

        // Lookup field in the schema's lookup table
        auto idx = lookup_field<T, Config>(key_name);
        if(idx) {
            auto field_status = dispatch_slot_deserialize<T, Config, E>(d, *idx, v);
            if(!field_status) {
                auto err = std::move(field_status).error();
                err.prepend_field(key_name);
                return std::unexpected(std::move(err));
            }
            seen_fields |= (std::uint64_t(1) << *idx);
            continue;
        }

        // Try flatten fields
        if constexpr(has_flatten_fields<T>()) {
            // TODO: flatten support in new dispatch
        }

        if constexpr(schema::deny_unknown) {
            return std::unexpected(E::unknown_field(key_name));
        } else {
            KOTA_EXPECTED_TRY(d.skip_field_value());
        }
    }

    // Check required fields
    constexpr std::uint64_t required = required_field_mask<T>();
    if((seen_fields & required) != required) {
        constexpr auto table = make_field_table<T, Config>();
        std::uint64_t missing = required & ~seen_fields;
        for(const auto& entry: table) {
            if(!entry.is_alias && (missing & (std::uint64_t(1) << entry.index))) {
                return std::unexpected(E::missing_field(entry.name));
            }
        }
        return std::unexpected(E::missing_field("unknown"));
    }

    return d.end_object();
}

/// Virtual-schema-driven struct deserialization (by_position mode).
template <typename Config, typename E, typename D, typename T>
auto struct_deserialize_by_position(D& d, T& v) -> std::expected<void, E> {
    using schema = meta::virtual_schema<T, Config>;
    using slots = typename schema::slots;
    constexpr std::size_t N = type_list_size_v<slots>;

    std::expected<void, E> status{};
    bool ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return ([&] {
            using slot_t = type_list_element_t<Is, slots>;
            using raw_t = std::remove_cv_t<typename slot_t::raw_type>;
            using attrs_t = typename slot_t::attrs;

            constexpr std::size_t offset = schema::fields[Is].offset;
            auto* base = reinterpret_cast<std::byte*>(std::addressof(v));
            auto& field_value = *reinterpret_cast<raw_t*>(base + offset);

            auto r = deserialize_slot_value<attrs_t, E>(d, field_value);
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

/// Entry point: detect field_mode and dispatch.
template <typename Config, typename E, typename D, typename T>
auto struct_deserialize(D& d, T& v) -> std::expected<void, E> {
    if constexpr(D::field_mode_v == field_mode::by_name) {
        return struct_deserialize_by_name<Config, E>(d, v);
    } else if constexpr(D::field_mode_v == field_mode::by_position) {
        return struct_deserialize_by_position<Config, E>(d, v);
    } else {
        static_assert(sizeof(D) == 0, "by_tag not yet implemented");
    }
}

}  // namespace kota::codec::detail
