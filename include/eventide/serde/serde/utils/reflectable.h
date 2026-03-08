#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "eventide/reflection/struct.h"
#include "eventide/serde/serde/annotation.h"
#include "eventide/serde/serde/attrs.h"
#include "eventide/serde/serde/attrs/behavior.h"
#include "eventide/serde/serde/attrs/schema.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/serde/traits.h"
#include "eventide/serde/serde/utils/field_dispatch.h"

namespace eventide::serde {

template <serializer_like S, typename V, typename T, typename E>
constexpr auto serialize(S& s, const V& v) -> std::expected<T, E>;

template <deserializer_like D, typename V, typename E>
constexpr auto deserialize(D& d, V& v) -> std::expected<void, E>;

namespace detail {

struct field_entry {
    std::string_view name;
    std::size_t index;
    bool has_explicit_rename;  // true if schema::rename was used (bypass config rename)
    bool is_alias;             // true if this is an alias entry
};

template <typename Config>
inline auto effective_wire_name(const field_entry& entry, std::string& scratch)
    -> std::string_view {
    if(entry.has_explicit_rename || entry.is_alias) {
        return entry.name;
    }
    return config::apply_field_rename<Config>(true, entry.name, scratch);
}

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

    std::string scratch;
    for(const auto& entry: table) {
        if(effective_wire_name<Config>(entry, scratch) == key) {
            return entry.index;
        }
    }
    return std::nullopt;
}

/// True if two different fields collapse to the same effective wire name.
/// This includes explicit rename, aliases, and Config-driven renaming.
template <typename T, typename Config>
auto has_ambiguous_wire_names() -> bool {
    const static bool ambiguous = [] {
        constexpr auto table = make_field_table<T, Config>();
        std::string left_scratch;
        std::string right_scratch;

        for(std::size_t i = 0; i < table.size(); ++i) {
            auto left_name = effective_wire_name<Config>(table[i], left_scratch);
            for(std::size_t j = i + 1; j < table.size(); ++j) {
                if(table[i].index == table[j].index) {
                    continue;
                }

                auto right_name = effective_wire_name<Config>(table[j], right_scratch);
                if(left_name == right_name) {
                    return true;
                }
            }
        }
        return false;
    }();
    return ambiguous;
}

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
        const bool matched =
            (([&]() -> bool {
                 if constexpr(schema::is_field_excluded<T, Is>()) {
                     return false;
                 } else {
                     return Is == index &&
                            (result = deserialize_field_at<T, Config, Is, E>(d_struct, value),
                             true);
                 }
             }()) ||
             ...);
        if(!matched) {
            return std::unexpected(E::type_mismatch);
        }
        return result;
    }(std::make_index_sequence<refl::field_count<T>()>{});
}

/// Check if struct T has any flatten fields.
template <typename T>
consteval bool has_flatten_fields() {
    return []<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        return (schema::is_field_flattened<T, Is>() || ...);
    }(std::make_index_sequence<refl::field_count<T>()>{});
}

/// Try to match a key against flatten fields. Returns true if matched.
template <typename T, typename Config, typename E, typename DeserializeStruct>
auto try_flatten_fields(std::string_view key_name, DeserializeStruct& d_struct, T& value)
    -> std::expected<bool, E> {
    bool matched = false;
    std::expected<void, E> nested_error;

    refl::for_each(value, [&](auto field) {
        if(matched) {
            return false;
        }

        using field_t = typename std::remove_cvref_t<decltype(field)>::type;
        if constexpr(!annotated_type<field_t>) {
            return true;
        } else {
            using attrs_t = typename std::remove_cvref_t<field_t>::attrs;
            if constexpr(tuple_has_v<attrs_t, schema::flatten>) {
                auto status = deserialize_struct_field<Config, E>(d_struct, key_name, field);
                if(!status) {
                    nested_error = std::unexpected(status.error());
                    return false;
                }
                if(*status) {
                    matched = true;
                    return false;
                }
            }
            return true;
        }
    });

    if(!nested_error) {
        return std::unexpected(nested_error.error());
    }
    return matched;
}

template <typename BaseConfig,
          typename Attrs,
          bool HasRenameAll = tuple_has_spec_v<Attrs, schema::rename_all>>
struct annotated_struct_config {
    using type = BaseConfig;
};

template <typename BaseConfig, typename Attrs>
struct annotated_struct_config<BaseConfig, Attrs, true> {
    using field_rename_policy = typename tuple_find_spec_t<Attrs, schema::rename_all>::policy;

    struct type {
        using field_rename = field_rename_policy;
    };
};

template <typename BaseConfig, typename Attrs>
using annotated_struct_config_t = typename annotated_struct_config<BaseConfig, Attrs>::type;

template <typename Config, typename E, serializer_like S, typename V>
    requires refl::reflectable_class<std::remove_cvref_t<V>>
constexpr auto serialize_reflectable(S& s, const V& v) -> std::expected<typename S::value_type, E> {
    using value_t = std::remove_cvref_t<V>;

    auto s_struct = s.serialize_struct(refl::type_name<value_t>(), refl::field_count<value_t>());
    if(!s_struct) {
        return std::unexpected(s_struct.error());
    }

    std::expected<void, E> field_result;
    refl::for_each(v, [&](auto field) {
        auto result = serialize_struct_field<Config, E>(*s_struct, field);
        if(!result) {
            field_result = std::unexpected(result.error());
            return false;
        }
        return true;
    });
    if(!field_result) {
        return std::unexpected(field_result.error());
    }

    return s_struct->end();
}

template <typename Config, typename E, bool DenyUnknown, deserializer_like D, typename V>
    requires refl::reflectable_class<std::remove_cvref_t<V>>
constexpr auto deserialize_reflectable(D& d, V& v) -> std::expected<void, E> {
    using value_t = std::remove_cvref_t<V>;

    if(has_ambiguous_wire_names<value_t, Config>()) {
        return std::unexpected(E::invalid_state);
    }

    auto d_struct = d.deserialize_struct(refl::type_name<value_t>(), refl::field_count<value_t>());
    if(!d_struct) {
        return std::unexpected(d_struct.error());
    }

    while(true) {
        auto key = d_struct->next_key();
        if(!key) {
            return std::unexpected(key.error());
        }
        if(!key->has_value()) {
            break;
        }

        std::string_view key_name = **key;

        auto idx = lookup_field<value_t, Config>(key_name);
        if(idx) {
            auto status = dispatch_field_by_index<value_t, Config, E>(*idx, *d_struct, v);
            if(!status) {
                return std::unexpected(status.error());
            }
            continue;
        }

        bool flatten_matched = false;
        if constexpr(has_flatten_fields<value_t>()) {
            auto flatten_status = try_flatten_fields<value_t, Config, E>(key_name, *d_struct, v);
            if(!flatten_status) {
                return std::unexpected(flatten_status.error());
            }
            flatten_matched = *flatten_status;
        }

        if(flatten_matched) {
            continue;
        }

        if constexpr(DenyUnknown) {
            return std::unexpected(E::type_mismatch);
        } else {
            auto skipped = d_struct->skip_value();
            if(!skipped) {
                return std::unexpected(skipped.error());
            }
        }
    }

    return d_struct->end();
}

}  // namespace detail

}  // namespace eventide::serde
