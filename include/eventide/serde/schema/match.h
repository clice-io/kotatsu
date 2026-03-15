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
#include <vector>

#include "eventide/reflection/struct.h"
#include "eventide/serde/schema/attrs.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/schema/descriptor.h"
#include "eventide/serde/schema/kind.h"
#include "eventide/serde/schema/node.h"
#include "eventide/serde/serde/utils/fwd.h"

namespace eventide::serde::schema {

namespace detail {

// ─── Kind compatibility ─────────────────────────────────────────────────────

constexpr bool kind_compatible(type_kind incoming, type_kind schema_kind) {
    if(incoming == type_kind::any || schema_kind == type_kind::any) return true;
    if(incoming == schema_kind) return true;
    if(incoming == type_kind::integer && schema_kind == type_kind::floating) return true;
    if(incoming == type_kind::floating && schema_kind == type_kind::integer) return true;
    if(incoming == type_kind::integer && schema_kind == type_kind::enumeration) return true;
    if(incoming == type_kind::enumeration && schema_kind == type_kind::integer) return true;
    if(incoming == type_kind::structure && schema_kind == type_kind::map) return true;
    if(incoming == type_kind::map && schema_kind == type_kind::structure) return true;
    if(incoming == type_kind::string && schema_kind == type_kind::character) return true;
    if(incoming == type_kind::character && schema_kind == type_kind::string) return true;
    if(incoming == type_kind::array && (schema_kind == type_kind::set ||
       schema_kind == type_kind::tuple || schema_kind == type_kind::bytes)) return true;
    if(schema_kind == type_kind::array && (incoming == type_kind::set ||
       incoming == type_kind::tuple || incoming == type_kind::bytes)) return true;
    return false;
}

/// Check if a type_hint bitmask is compatible with a schema type_kind.
constexpr bool hint_compatible(serde::type_hint hints, type_kind schema_kind) {
    auto bits = static_cast<std::uint8_t>(hints);
    auto target = hint_to_kind(bits);
    return kind_compatible(target, schema_kind);
}

// ─── Compile-time field entry (uses string_view) ────────────────────────────

struct ct_field_entry {
    std::string_view wire_name;
    type_kind kind = type_kind::any;
    bool required = false;
};

// ─── Count entries (fields + aliases, flatten-expanded) ─────────────────────

template <typename T>
consteval std::size_t count_match_entries();

template <typename T, std::size_t I>
consteval std::size_t single_match_contribution() {
    if constexpr(field_has_skip<T, I>()) {
        return 0;
    } else if constexpr(field_has_flatten<T, I>()) {
        using field_t = refl::field_type<T, I>;
        using inner_t = unwrapped_t<field_t>;
        static_assert(refl::reflectable_class<inner_t>);
        return count_match_entries<inner_t>();
    } else {
        return 1 + serde::schema::detail::alias_count<T, I>();
    }
}

template <typename T>
consteval std::size_t count_match_entries() {
    if constexpr(!refl::reflectable_class<std::remove_cvref_t<T>>) {
        return 0;
    } else {
        using U = std::remove_cvref_t<T>;
        constexpr auto N = refl::field_count<U>();
        if constexpr(N == 0) {
            return 0;
        } else {
            return []<std::size_t... Is>(std::index_sequence<Is...>) consteval {
                return (single_match_contribution<U, Is>() + ...);
            }(std::make_index_sequence<N>{});
        }
    }
}

// ─── Count required fields (unique fields only, no aliases) ─────────────────

template <typename T>
consteval std::size_t count_required_fields();

template <typename T, std::size_t I>
consteval std::size_t single_required_contribution() {
    if constexpr(field_has_skip<T, I>()) {
        return 0;
    } else if constexpr(field_has_flatten<T, I>()) {
        using field_t = refl::field_type<T, I>;
        using inner_t = unwrapped_t<field_t>;
        return count_required_fields<inner_t>();
    } else {
        return resolve_field_required<T, I>() ? 1 : 0;
    }
}

template <typename T>
consteval std::size_t count_required_fields() {
    if constexpr(!refl::reflectable_class<std::remove_cvref_t<T>>) {
        return 0;
    } else {
        using U = std::remove_cvref_t<T>;
        constexpr auto N = refl::field_count<U>();
        if constexpr(N == 0) {
            return 0;
        } else {
            return []<std::size_t... Is>(std::index_sequence<Is...>) consteval {
                return (single_required_contribution<U, Is>() + ...);
            }(std::make_index_sequence<N>{});
        }
    }
}

// ─── Fill entries (recursive for flatten, includes aliases) ─────────────────

template <typename T, typename Config, std::size_t N>
consteval void fill_field_entries(std::array<ct_field_entry, N>& entries, std::size_t& pos);

template <typename T, typename Config, std::size_t I, std::size_t N>
consteval void fill_one_field_entry(std::array<ct_field_entry, N>& entries, std::size_t& pos) {
    if constexpr(field_has_skip<T, I>()) {
        // nothing
    } else if constexpr(field_has_flatten<T, I>()) {
        using field_t = refl::field_type<T, I>;
        using inner_t = unwrapped_t<field_t>;
        fill_field_entries<inner_t, Config>(entries, pos);
    } else {
        constexpr auto kind = resolve_field_kind<T, I>();
        constexpr auto required = resolve_field_required<T, I>();

        entries[pos++] = {resolve_wire_name<T, I, Config>(), kind, required};

        if constexpr(field_has_alias<T, I>()) {
            using field_t = refl::field_type<T, I>;
            using attrs_t = typename field_t::attrs;
            using alias_attr = serde::detail::tuple_find_t<attrs_t, is_alias_attr>;
            for(auto alias_name: alias_attr::names) {
                entries[pos++] = {alias_name, kind, required};
            }
        }
    }
}

template <typename T, typename Config, std::size_t N>
consteval void fill_field_entries(std::array<ct_field_entry, N>& entries, std::size_t& pos) {
    using U = std::remove_cvref_t<T>;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        (fill_one_field_entry<U, Config, Is>(entries, pos), ...);
    }(std::make_index_sequence<refl::field_count<U>()>{});
}

// ─── Build sorted entry table ───────────────────────────────────────────────

template <typename T, typename Config>
    requires refl::reflectable_class<std::remove_cvref_t<T>>
consteval auto build_field_entries() {
    constexpr std::size_t N = count_match_entries<T>();
    std::array<ct_field_entry, N> entries{};
    std::size_t pos = 0;
    fill_field_entries<T, Config>(entries, pos);

    for(std::size_t i = 1; i < N; i++) {
        for(std::size_t j = i;
            j > 0 && entries[j].wire_name.size() < entries[j - 1].wire_name.size(); j--) {
            auto tmp = entries[j];
            entries[j] = entries[j - 1];
            entries[j - 1] = tmp;
        }
    }
    return entries;
}

// ─── Fold-based key matcher ─────────────────────────────────────────────────

struct match_result {
    type_kind kind;
    bool required;
};

template <typename T, typename Config>
    requires refl::reflectable_class<std::remove_cvref_t<T>>
inline auto match_key(std::string_view key) -> std::optional<match_result> {
    static constexpr auto entries = build_field_entries<T, Config>();
    constexpr auto N = entries.size();

    if constexpr(N == 0) {
        return std::nullopt;
    } else {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) -> std::optional<match_result> {
            std::optional<match_result> r;
            (void)((key.size() == entries[Is].wire_name.size() &&
                    key == entries[Is].wire_name &&
                    (r.emplace(match_result{entries[Is].kind, entries[Is].required}), true)) ||
                   ...);
            return r;
        }(std::make_index_sequence<N>{});
    }
}

}  // namespace detail

// ─── Variant matching via schema_node ───────────────────────────────────────

/// Match a schema_node against variant alternative schemas.
/// For object nodes, uses compile-time field tables for matching.
/// Returns the index of the best-matching alternative, or nullopt.
template <typename Config, typename... Ts>
auto match_variant(const schema_node& node) -> std::optional<std::size_t> {
    if(node.fields.empty()) return std::nullopt;

    std::optional<std::size_t> best_index;
    std::size_t best_score = 0;

    auto try_one = [&]<std::size_t I>() {
        using Alt = std::variant_alternative_t<I, std::variant<Ts...>>;
        using U = std::remove_cvref_t<Alt>;

        if constexpr(!refl::reflectable_class<U>) {
            return;
        } else {
            static constexpr auto req_count = detail::count_required_fields<U>();

            std::size_t matched = 0;
            std::size_t required_matched = 0;

            for(const auto& f: node.fields) {
                auto r = detail::match_key<U, Config>(f.key);
                if(!r) continue;
                if(f.value_hints != serde::type_hint::any) {
                    auto incoming_kind = hint_to_kind(static_cast<std::uint8_t>(f.value_hints));
                    if(!detail::kind_compatible(incoming_kind, r->kind)) {
                        return;
                    }
                }
                matched++;
                if(r->required) required_matched++;
            }

            if(required_matched < req_count) return;
            if(!best_index.has_value() || matched > best_score) {
                best_index = I;
                best_score = matched;
            }
        }
    };

    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (try_one.template operator()<Is>(), ...);
    }(std::make_index_sequence<sizeof...(Ts)>{});

    return best_index;
}

// ─── Kind-to-index map for primitive dispatch ───────────────────────────────
// Maps type_hint bit positions (0-7) to variant alternative indices.

template <std::size_t N>
struct kind_to_indices_map {
    static constexpr std::size_t max_per_kind = N < 1 ? 1 : N;
    std::array<std::array<std::size_t, max_per_kind>, 8> indices{};
    std::array<std::size_t, 8> count{};
};

template <typename... Ts>
consteval auto build_kind_to_index_map() {
    kind_to_indices_map<sizeof...(Ts)> map{};

    auto try_one = [&]<std::size_t I>() consteval {
        using Alt = std::variant_alternative_t<I, std::variant<Ts...>>;
        auto ki = kind_to_hint_index(kind_of<Alt>());
        if(ki < 8 && map.count[ki] < sizeof...(Ts)) {
            map.indices[ki][map.count[ki]] = I;
            map.count[ki]++;
        }
    };

    [&]<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        (try_one.template operator()<Is>(), ...);
    }(std::make_index_sequence<sizeof...(Ts)>{});

    return map;
}

// ─── Dispatch helpers ───────────────────────────────────────────────────────

/// Deserialize variant alternative at a runtime index.
template <typename D, typename... Ts, std::size_t... Is>
auto deserialize_variant_at_impl(std::size_t idx,
                                 D& d,
                                 std::variant<Ts...>& v,
                                 std::index_sequence<Is...>)
    -> std::expected<void, typename D::error_type> {
    using E = typename D::error_type;
    std::expected<void, E> result = std::unexpected(E::type_mismatch);
    bool matched = false;

    (([&] {
         if(matched || Is != idx) return;
         matched = true;
         using alt_t = std::variant_alternative_t<Is, std::variant<Ts...>>;
         if constexpr(std::default_initializable<alt_t>) {
             alt_t alt{};
             auto status = serde::deserialize(d, alt);
             if(!status) {
                 result = std::unexpected(status.error());
                 return;
             }
             auto finished = d.finish();
             if(!finished) {
                 result = std::unexpected(finished.error());
                 return;
             }
             v = std::move(alt);
             result = {};
         } else {
             result = std::unexpected(E::invalid_state);
         }
     }()),
     ...);

    return result;
}

template <typename D, typename... Ts>
auto deserialize_variant_at(D& d, std::size_t idx, std::variant<Ts...>& v)
    -> std::expected<void, typename D::error_type> {
    return deserialize_variant_at_impl(idx, d, v, std::make_index_sequence<sizeof...(Ts)>{});
}

/// Full untagged variant dispatch: schema matching + primitive fallback.
/// MakeDeser: callable returning a fresh D from saved source.
template <typename D, typename Config, typename... Ts, typename MakeDeser>
auto untagged_dispatch(std::variant<Ts...>& v,
                       const schema_node& node,
                       MakeDeser&& make_deser) -> std::expected<void, typename D::error_type> {
    using E = typename D::error_type;
    auto hint_bits = static_cast<std::uint8_t>(node.hints);

    // Object matching via compile-time schema
    if(hint_bits & static_cast<std::uint8_t>(serde::type_hint::object)) {
        auto idx = match_variant<Config, Ts...>(node);
        if(idx.has_value()) {
            D d2 = make_deser();
            return deserialize_variant_at<D, Ts...>(d2, *idx, v);
        }
    }

    // Primitive/kind-based matching
    constexpr auto kind_map = build_kind_to_index_map<Ts...>();
    constexpr std::uint8_t bit_for_kind[] = {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
    };

    std::array<std::size_t, sizeof...(Ts)> candidates{};
    std::size_t n_candidates = 0;
    for(std::size_t ki = 0; ki < 8; ki++) {
        if(hint_bits & bit_for_kind[ki]) {
            for(std::size_t ci = 0; ci < kind_map.count[ki]; ci++) {
                auto idx = kind_map.indices[ki][ci];
                bool dup = false;
                for(std::size_t j = 0; j < n_candidates; j++) {
                    if(candidates[j] == idx) {
                        dup = true;
                        break;
                    }
                }
                if(!dup) {
                    candidates[n_candidates++] = idx;
                }
            }
        }
    }

    for(std::size_t i = 1; i < n_candidates; i++) {
        for(std::size_t j = i; j > 0 && candidates[j] < candidates[j - 1]; j--) {
            std::swap(candidates[j], candidates[j - 1]);
        }
    }

    for(std::size_t ci = 0; ci < n_candidates; ci++) {
        D d2 = make_deser();
        auto result = deserialize_variant_at<D, Ts...>(d2, candidates[ci], v);
        if(result.has_value()) {
            return result;
        }
        if(ci + 1 == n_candidates) {
            return result;
        }
    }

    return std::unexpected(E::type_mismatch);
}

}  // namespace eventide::serde::schema
