#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

#include "eventide/serde/serde/attrs/schema.h"
#include "eventide/serde/serde/config.h"

namespace eventide::serde::detail {

// ── Compile-time field descriptor table ────────────────────────────

struct field_entry {
    std::string_view name;
    std::size_t index;
    bool has_explicit_rename;  // true if schema::rename was used (bypass config rename)
    bool is_alias;             // true if this is an alias entry
};

/// Count of non-excluded (non-skip, non-flatten) fields in struct T.
template <typename T>
consteval std::size_t count_lookup_fields() {
    return []<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        return ((schema::is_field_excluded<T, Is>() ? 0 : 1) + ...);
    }(std::make_index_sequence<refl::field_count<T>()>{});
}

/// Count total aliases across all non-excluded fields.
template <typename T>
consteval std::size_t count_lookup_aliases() {
    return []<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        return ((schema::is_field_excluded<T, Is>() ? 0 : schema::detail::alias_count<T, Is>()) +
                ...);
    }(std::make_index_sequence<refl::field_count<T>()>{});
}

/// Build the canonical-name lookup table for struct T.
/// Each entry maps a wire name to its field index.
template <typename T, typename Config>
consteval auto make_field_table() {
    constexpr std::size_t N = count_lookup_fields<T>();
    constexpr std::size_t A = count_lookup_aliases<T>();
    constexpr std::size_t total = N + A;

    std::array<field_entry, total> table{};
    std::size_t pos = 0;

    auto fill = [&]<std::size_t I>() consteval {
        if constexpr(schema::is_field_excluded<T, I>()) {
            return;
        } else {
            using field_t = refl::field_type<T, I>;
            constexpr bool has_rename = []() consteval {
                if constexpr(serde::has_attrs<field_t>) {
                    return serde::detail::tuple_any_of_v<typename field_t::attrs, is_rename_attr>;
                } else {
                    return false;
                }
            }();

            // Canonical name
            table[pos++] = {schema::canonical_field_name<T, I>(), I, has_rename, false};

            // Aliases
            if constexpr(schema::detail::field_has_alias<T, I>()) {
                using attrs_t = typename field_t::attrs;
                using alias_attr = serde::detail::tuple_find_t<attrs_t, is_alias_attr>;
                for(auto alias_name: alias_attr::names) {
                    table[pos++] = {alias_name, I, true, true};
                }
            }
        }
    };

    [&]<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        (fill.template operator()<Is>(), ...);
    }(std::make_index_sequence<refl::field_count<T>()>{});

    return table;
}

/// Lookup a field index by key name. Returns nullopt if not found.
/// Applies Config rename policy to transform canonical names to wire names.
template <typename T, typename Config>
auto lookup_field(std::string_view key) -> std::optional<std::size_t> {
    constexpr auto table = make_field_table<T, Config>();

    if constexpr(requires { typename Config::field_rename; }) {
        std::string scratch;
        for(const auto& entry: table) {
            if(entry.has_explicit_rename || entry.is_alias) {
                // schema::rename or alias — already the wire name
                if(entry.name == key) {
                    return entry.index;
                }
            } else {
                // Apply config rename to get wire name
                auto wire_name = config::apply_field_rename<Config>(true, entry.name, scratch);
                if(wire_name == key) {
                    return entry.index;
                }
            }
        }
        return std::nullopt;
    } else {
        for(const auto& entry: table) {
            if(entry.name == key) {
                return entry.index;
            }
        }
        return std::nullopt;
    }
}

// ── Index-based field dispatch ─────────────────────────────────────

/// Deserialize a single struct field by its compile-time index.
template <typename T, typename Config, std::size_t I, typename E, typename DeserializeStruct>
auto deserialize_field_at(DeserializeStruct& d_struct, T& value) -> std::expected<void, E> {
    refl::field<I, T> field{value};
    using field_t = typename decltype(field)::type;

    if constexpr(!annotated_type<field_t>) {
        auto result = d_struct.deserialize_value(field.value());
        if(!result) {
            return std::unexpected(result.error());
        }
        return {};
    } else {
        using attrs_t = typename std::remove_cvref_t<field_t>::attrs;
        auto&& fval = annotated_value(field.value());
        using value_t = std::remove_cvref_t<decltype(fval)>;

        // Behavior: skip_if — conditionally skip deserialization
        if constexpr(tuple_has_spec_v<attrs_t, behavior::skip_if>) {
            using Pred = typename tuple_find_spec_t<attrs_t, behavior::skip_if>::predicate;
            if(evaluate_skip_predicate<Pred>(fval, false)) {
                auto skip_result = d_struct.skip_value();
                if(!skip_result) {
                    return std::unexpected(skip_result.error());
                }
                return {};
            }
        }

        // Behavior: with<Adapter> — adapter-based deserialization
        if constexpr(tuple_has_spec_v<attrs_t, behavior::with>) {
            using Adapter = typename tuple_find_spec_t<attrs_t, behavior::with>::adapter;
            auto result = Adapter::deserialize_field(d_struct, fval);
            if(!result) {
                return std::unexpected(result.error());
            }
            return {};
        }
        // Behavior: enum_string — deserialize string then map to enum
        else if constexpr(tuple_has_spec_v<attrs_t, behavior::enum_string>) {
            using Policy = typename tuple_find_spec_t<attrs_t, behavior::enum_string>::policy;
            static_assert(std::is_enum_v<value_t>,
                          "behavior::enum_string requires an enum field type");
            std::string enum_text;
            auto result = d_struct.deserialize_value(enum_text);
            if(!result) {
                return std::unexpected(result.error());
            }
            auto parsed = spelling::map_string_to_enum<value_t, Policy>(enum_text);
            if(parsed.has_value()) {
                fval = *parsed;
                return {};
            } else {
                return std::unexpected(E::type_mismatch);
            }
        }
        // Default: deserialize value directly
        else {
            // For tagged variants, preserve annotation
            if constexpr(is_specialization_of<std::variant, value_t> &&
                         tuple_any_of_v<attrs_t, is_tagged_attr>) {
                auto result = d_struct.deserialize_value(field.value());
                if(!result) {
                    return std::unexpected(result.error());
                }
                return {};
            } else {
                auto result = d_struct.deserialize_value(fval);
                if(!result) {
                    return std::unexpected(result.error());
                }
                return {};
            }
        }
    }
}

/// Dispatch to the correct field deserializer by runtime index.
template <typename T, typename Config, typename E, typename DeserializeStruct>
auto dispatch_field_by_index(std::size_t index, DeserializeStruct& d_struct, T& value)
    -> std::expected<void, E> {
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) -> std::expected<void, E> {
        std::expected<void, E> result;
        bool dispatched = false;
        ((Is == index ? (result = deserialize_field_at<T, Config, Is, E>(d_struct, value),
                         dispatched = true,
                         true)
                      : false) ||
         ...);
        if(!dispatched) {
            return std::unexpected(E::type_mismatch);
        }
        return result;
    }(std::make_index_sequence<refl::field_count<T>()>{});
}

}  // namespace eventide::serde::detail
