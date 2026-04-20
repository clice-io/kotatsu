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

namespace kota::codec::detail {

// Forward declarations for tagged variant dispatch (defined in tagged.h)
template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_externally_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E>;

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_internally_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E>;

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_adjacently_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E>;

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

    // Tagged variants: dispatch directly since we have the attrs but no annotation wrapper
    if constexpr(is_specialization_of<std::variant, std::remove_cvref_t<V>> &&
                 tuple_any_of_v<Attrs, meta::is_tagged_attr>) {
        using tag_attr = tuple_find_t<Attrs, meta::is_tagged_attr>;
        constexpr auto strategy = meta::tagged_strategy_of<tag_attr>;
        if constexpr(strategy == meta::tagged_strategy::external) {
            return deserialize_externally_tagged<E>(d, value, tag_attr{});
        } else if constexpr(strategy == meta::tagged_strategy::internal) {
            return deserialize_internally_tagged<E>(d, value, tag_attr{});
        } else {
            return deserialize_adjacently_tagged<E>(d, value, tag_attr{});
        }
    }

    // Default: plain value
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

/// Check whether any two fields in the virtual_schema share the same wire name or alias.
template <typename T, typename Config>
auto schema_has_ambiguous_wire_names() -> bool {
    const static bool ambiguous = [] {
        using schema = meta::virtual_schema<T, Config>;
        for(std::size_t i = 0; i < schema::count; ++i) {
            for(std::size_t j = i + 1; j < schema::count; ++j) {
                // name vs name
                if(schema::fields[i].name == schema::fields[j].name) {
                    return true;
                }
                // name vs aliases
                for(auto alias: schema::fields[j].aliases) {
                    if(schema::fields[i].name == alias) {
                        return true;
                    }
                }
                for(auto alias: schema::fields[i].aliases) {
                    if(alias == schema::fields[j].name) {
                        return true;
                    }
                }
                // aliases vs aliases
                for(auto a: schema::fields[i].aliases) {
                    for(auto b: schema::fields[j].aliases) {
                        if(a == b) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }();
    return ambiguous;
}

/// Lookup a wire name in the virtual_schema fields array (handles flatten + aliases).
template <typename T, typename Config>
auto schema_lookup_field(std::string_view key) -> std::optional<std::size_t> {
    using schema = meta::virtual_schema<T, Config>;
    for(std::size_t i = 0; i < schema::count; ++i) {
        if(schema::fields[i].name == key) {
            return i;
        }
        for(auto alias: schema::fields[i].aliases) {
            if(alias == key) {
                return i;
            }
        }
    }
    return std::nullopt;
}

/// Compute required field bitmask from virtual_schema slots.
template <typename T, typename Config>
consteval std::uint64_t schema_required_field_mask() {
    using schema = meta::virtual_schema<T, Config>;
    using slots = typename schema::slots;
    constexpr std::size_t N = type_list_size_v<slots>;
    static_assert(N <= 64, "schema_required_field_mask: >64 slots not supported");

    std::uint64_t mask = 0;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        (([&] {
             using slot_t = type_list_element_t<Is, slots>;
             using raw_t = std::remove_cv_t<typename slot_t::raw_type>;
             using attrs_t = typename slot_t::attrs;

             constexpr bool has_default = tuple_has_v<attrs_t, meta::attrs::default_value>;
             constexpr bool is_opt = is_specialization_of<std::optional, raw_t>;

             if constexpr(!has_default && !is_opt) {
                 mask |= (std::uint64_t(1) << Is);
             }
         }()),
         ...);
    }(std::make_index_sequence<N>{});

    return mask;
}

/// Virtual-schema-driven struct deserialization (by_name mode).
/// Requires the deserializer to provide:
///   d.begin_object()              → status_t
///   d.next_field()                → result_t<optional<string_view>>
///   d.skip_field_value()          → status_t
///   d.end_object()                → status_t
template <typename Config, typename E, typename D, typename T>
auto struct_deserialize_by_name(D& d, T& v) -> std::expected<void, E> {
    using schema = meta::virtual_schema<T, Config>;

    if(schema_has_ambiguous_wire_names<T, Config>()) {
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

        auto idx = schema_lookup_field<T, Config>(key_name);
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

        if constexpr(schema::deny_unknown) {
            return std::unexpected(E::unknown_field(key_name));
        } else {
            KOTA_EXPECTED_TRY(d.skip_field_value());
        }
    }

    // Check required fields
    constexpr std::uint64_t required = schema_required_field_mask<T, Config>();
    if((seen_fields & required) != required) {
        std::uint64_t missing = required & ~seen_fields;
        for(std::size_t i = 0; i < schema::count; ++i) {
            if(missing & (std::uint64_t(1) << i)) {
                return std::unexpected(E::missing_field(schema::fields[i].name));
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
