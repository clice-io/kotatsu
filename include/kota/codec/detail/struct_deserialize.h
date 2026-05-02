#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/support/expected_try.h"
#include "kota/support/string_match.h"
#include "kota/support/type_list.h"
#include "kota/support/type_traits.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/codec/detail/apply_behavior.h"
#include "kota/codec/detail/backend.h"
#include "kota/codec/detail/config.h"
#include "kota/codec/detail/struct_visitor.h"

namespace kota::codec::detail {

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_externally_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E>;

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_internally_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E>;

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_adjacently_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E>;

template <typename Attrs, typename E, typename D, typename V>
KOTA_ALWAYS_INLINE auto deserialize_slot_value(D& d, V& value) -> std::expected<void, E> {
    if constexpr(tuple_count_of_v<Attrs, meta::is_behavior_provider> > 0) {
        auto result = apply_deserialize_behavior<Attrs, V, E>(
            value,
            [&](auto& v) { return codec::deserialize(d, v); },
            [&](auto tag, auto& v) -> std::expected<void, E> {
                using Adapter = typename decltype(tag)::type;
                if constexpr(requires {
                                 Adapter::from_wire(std::declval<typename Adapter::wire_type>());
                             }) {
                    using wire_t = typename Adapter::wire_type;
                    wire_t wire{};
                    KOTA_EXPECTED_TRY(codec::deserialize(d, wire));
                    v = Adapter::from_wire(std::move(wire));
                    return {};
                } else if constexpr(requires { Adapter::deserialize(d, v); }) {
                    return Adapter::deserialize(d, v);
                } else {
                    return codec::deserialize(d, v);
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
            return deserialize_externally_tagged<E>(d, value, tag_attr{});
        } else if constexpr(strategy == meta::tagged_strategy::internal) {
            return deserialize_internally_tagged<E>(d, value, tag_attr{});
        } else {
            return deserialize_adjacently_tagged<E>(d, value, tag_attr{});
        }
    } else {
        return codec::deserialize(d, value);
    }
}

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

             if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::skip_if>) {
                 using pred =
                     typename tuple_find_spec_t<attrs_t, meta::behavior::skip_if>::predicate;
                 if(meta::evaluate_skip_predicate<pred>(field_value, false)) {
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

template <typename T, typename Config>
consteval bool schema_has_ambiguous_wire_names() {
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
}

template <typename T, typename Config>
struct field_name_table {
    using schema = meta::virtual_schema<T, Config>;

    static consteval std::size_t compute_name_count() {
        std::size_t count = 0;
        for(std::size_t i = 0; i < schema::count; ++i) {
            count += 1 + schema::fields[i].aliases.size();
        }
        return count;
    }

    constexpr static std::size_t name_count = compute_name_count();

    constexpr static auto names = [] {
        std::array<std::string_view, name_count> result{};
        std::size_t out = 0;
        for(std::size_t i = 0; i < schema::count; ++i) {
            result[out++] = schema::fields[i].name;
            for(auto alias: schema::fields[i].aliases) {
                result[out++] = alias;
            }
        }
        return result;
    }();

    constexpr static auto slot_map = [] {
        std::array<std::size_t, name_count> result{};
        std::size_t out = 0;
        for(std::size_t i = 0; i < schema::count; ++i) {
            result[out++] = i;
            for(std::size_t j = 0; j < schema::fields[i].aliases.size(); ++j) {
                result[out++] = i;
            }
        }
        return result;
    }();
};

template <typename T, typename Config>
auto schema_lookup_field(std::string_view key) -> std::optional<std::size_t> {
    using table = field_name_table<T, Config>;
    if constexpr(table::name_count == 0) {
        return std::nullopt;
    } else {
        auto idx = string_match<table::names>(key);
        if(idx) {
            return table::slot_map[*idx];
        }
        return std::nullopt;
    }
}

template <typename Slots, std::size_t I>
consteval std::uint64_t required_field_bit() {
    using slot_t = type_list_element_t<I, Slots>;
    using raw_t = std::remove_cv_t<typename slot_t::raw_type>;
    using attrs_t = typename slot_t::attrs;
    if constexpr(!tuple_has_v<attrs_t, meta::attrs::default_value> &&
                 !is_specialization_of<std::optional, raw_t>) {
        return std::uint64_t(1) << I;
    } else {
        return 0;
    }
}

template <typename Slots, std::size_t... Is>
consteval std::uint64_t required_field_mask_impl(std::index_sequence<Is...>) {
    if constexpr(sizeof...(Is) == 0) {
        return 0;
    } else {
        return (required_field_bit<Slots, Is>() | ...);
    }
}

template <typename T, typename Config>
consteval std::uint64_t schema_required_field_mask() {
    using schema = meta::virtual_schema<T, Config>;
    using slots = typename schema::slots;
    constexpr std::size_t N = type_list_size_v<slots>;
    static_assert(N <= 64, "schema_required_field_mask: >64 slots not supported");
    return required_field_mask_impl<slots>(std::make_index_sequence<N>{});
}

template <typename Config>
constexpr bool config_deny_unknown_v = requires { requires Config::deny_unknown_fields; };

template <typename Config, typename E, typename D, typename T>
auto struct_deserialize_by_name(D& d, T& v) -> std::expected<void, E> {
    using schema = meta::virtual_schema<T, Config>;
    constexpr bool deny_unknown = schema::deny_unknown || config_deny_unknown_v<Config>;

    if constexpr(schema_has_ambiguous_wire_names<T, Config>()) {
        return std::unexpected(E::invalid_state);
    } else {
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
                // Copy key before dispatch: nested begin_object() may realloc the
                // deserializer's frame stack, invalidating the string_view.
                std::string owned_key(key_name);
                auto field_status = dispatch_slot_deserialize<T, Config, E>(d, *idx, v);
                if(!field_status) {
                    auto err = std::move(field_status).error();
                    err.prepend_field(owned_key);
                    return std::unexpected(std::move(err));
                }
                seen_fields |= (std::uint64_t(1) << *idx);
                continue;
            }

            if constexpr(deny_unknown) {
                return std::unexpected(E::unknown_field(std::string(key_name)));
            } else {
                KOTA_EXPECTED_TRY(d.skip_field_value());
            }
        }

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
}

template <typename E, typename D>
struct deserialize_by_position_visitor {
    using error_type = E;
    D& d;

    template <std::size_t I, typename raw_t, typename attrs_t>
    auto on_field(raw_t& field_ref, std::string_view) -> std::expected<void, E> {
        return deserialize_slot_value<attrs_t, E>(d, field_ref);
    }

    template <std::size_t I, typename raw_t, typename attrs_t>
    auto on_skip(raw_t&) -> std::expected<void, E> {
        // Discard using the same wire shape as normal deserialization
        // (respects with/as/enum_string behavior attrs).
        raw_t discard{};
        return deserialize_slot_value<attrs_t, E>(d, discard);
    }
};

template <typename Config, typename E, typename D, typename T>
auto struct_deserialize_by_position(D& d, T& v) -> std::expected<void, E> {
    deserialize_by_position_visitor<E, D> visitor{d};
    return for_each_field<Config, false>(v, visitor);
}

template <typename Config, typename E, typename D, typename T>
KOTA_ALWAYS_INLINE auto struct_deserialize(D& d, T& v) -> std::expected<void, E> {
    if constexpr(D::field_mode_v == field_mode::by_name) {
        return struct_deserialize_by_name<Config, E>(d, v);
    } else if constexpr(D::field_mode_v == field_mode::by_position) {
        return struct_deserialize_by_position<Config, E>(d, v);
    } else {
        static_assert(sizeof(D) == 0, "by_tag not yet implemented");
    }
}

}  // namespace kota::codec::detail
