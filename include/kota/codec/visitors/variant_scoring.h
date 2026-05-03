#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/support/type_list.h"
#include "kota/support/type_traits.h"
#include "kota/meta/type_info.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/detail/variant_dispatch.h"

namespace kota::codec {

template <typename Backend, typename T>
auto deserialize(typename Backend::value_type& src, T& out) -> typename Backend::error_type;

/// Adapter shim that bridges Backend's visit_object_keys/visit_array_keys
/// to the source_adapter interface expected by multi_score.
template <typename Backend>
struct backend_source_adapter {
    struct node_type {
        typename Backend::value_type* val = nullptr;
    };

    static meta::type_kind kind_of(node_type n) {
        if(!n.val)
            return meta::type_kind::unknown;
        return Backend::kind_of(*n.val);
    }

    template <typename Fn>
    static void for_each_field(node_type n, Fn&& fn) {
        if(!n.val)
            return;
        struct key_collector {
            Fn& fn_ref;
            using node_t = node_type;

            typename Backend::error_type on_field(std::string_view key,
                                                  meta::type_kind,
                                                  typename Backend::value_type& val) {
                fn_ref(key, node_t{&val});
                return Backend::success;
            }
        };
        key_collector collector{fn};
        Backend::visit_object_keys(*n.val, collector);
    }

    template <typename Fn>
    static void for_each_element(node_type n, Fn&& fn) {
        if(!n.val)
            return;
        struct elem_collector {
            Fn& fn_ref;
            using node_t = node_type;

            typename Backend::error_type on_element(std::size_t idx,
                                                    std::size_t total,
                                                    meta::type_kind,
                                                    typename Backend::value_type& val) {
                fn_ref(idx, total, node_t{&val});
                return Backend::success;
            }
        };
        elem_collector collector{fn};
        Backend::visit_array_keys(*n.val, collector);
    }
};

/// select_variant_index_v2: uses Backend for scoring instead of source_adapter
template <typename Backend, typename Config, typename... Ts>
auto select_variant_index_v2(typename Backend::value_type& src) -> std::optional<std::size_t> {
    constexpr std::size_t N = sizeof...(Ts);
    static_assert(N <= 64, "variant with more than 64 alternatives is not supported");

    auto source_kind = Backend::kind_of(src);

    auto [live, live_count] = detail::build_live_mask<Ts...>(source_kind);

    if(live_count == 0)
        return std::nullopt;
    if(live_count == 1)
        return static_cast<std::size_t>(std::countr_zero(live));

    // For object/array types, use multi_score with a backend adapter shim
    if(meta::is_object_kind(source_kind) || meta::is_sequence_kind(source_kind)) {
        using adapter = backend_source_adapter<Backend>;

        constexpr meta::type_info_fn info_fns[] = {&meta::type_info_of<Ts, Config>...};
        std::size_t scores[N] = {};

        detail::variant_candidate candidates[64];
        std::size_t cand_count = 0;
        {
            std::uint64_t mask = live;
            while(mask) {
                std::size_t idx = static_cast<std::size_t>(std::countr_zero(mask));
                candidates[cand_count++] = {idx, &info_fns[idx]()};
                mask &= mask - 1;
            }
        }

        detail::multi_score<adapter>(
            typename adapter::node_type{&src}, candidates, cand_count, scores);

        std::size_t best_score = 0;
        std::size_t best_count = 0;
        std::optional<std::size_t> best_idx;
        {
            std::uint64_t mask = live;
            while(mask) {
                std::size_t idx = static_cast<std::size_t>(std::countr_zero(mask));
                if(scores[idx] > best_score) {
                    best_score = scores[idx];
                    best_idx = idx;
                    best_count = 1;
                } else if(scores[idx] == best_score) {
                    ++best_count;
                }
                mask &= mask - 1;
            }
        }

        if(best_idx && best_count == 1)
            return best_idx;
    }

    // Fall back to kind-based selection
    return detail::select_by_kind<Config, Ts...>(live, source_kind);
}

/// deserialize_variant_at_v2: deserialize into variant at index
template <typename Backend, typename... Ts>
auto deserialize_variant_at_v2(std::size_t idx,
                               typename Backend::value_type& src,
                               std::variant<Ts...>& out) -> typename Backend::error_type {
    using E = typename Backend::error_type;
    E err = Backend::type_mismatch;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (void)((Is == idx ? (err = [&] {
                                 using alt_t = std::variant_alternative_t<Is, std::variant<Ts...>>;
                                 alt_t alt{};
                                 auto e = deserialize<Backend>(src, alt);
                                 if(e != Backend::success)
                                     return e;
                                 out = std::move(alt);
                                 return Backend::success;
                             }(),
                             true)
                          : false) ||
               ...);
    }(std::index_sequence_for<Ts...>{});
    return err;
}

/// Concept: backend supports capturing raw JSON from a value and re-parsing.
template <typename Backend>
concept reparse_backend = requires(typename Backend::value_type& v) {
    { Backend::capture_raw_json(v) } -> std::same_as<std::pair<std::string, typename Backend::error_type>>;
    {
        Backend::with_reparsed(
            std::string_view{},
            [](typename Backend::value_type&) -> typename Backend::error_type {
                return Backend::success;
            })
    } -> std::same_as<typename Backend::error_type>;
};

/// deserialize_variant_untagged: select best alternative then deserialize.
/// For reparse-capable backends (e.g. simdjson where forward-only
/// iteration limits scoring accuracy), tries alternatives ranked by score,
/// re-parsing for each attempt so that a failed deserialization can fall
/// back to the next candidate.
template <typename Backend, typename Config, typename... Ts>
auto deserialize_variant_untagged(typename Backend::value_type& src, std::variant<Ts...>& out)
    -> typename Backend::error_type {
    if constexpr(!reparse_backend<Backend>) {
        auto idx = select_variant_index_v2<Backend, Config, Ts...>(src);
        if(!idx)
            return Backend::type_mismatch;
        return deserialize_variant_at_v2<Backend>(*idx, src, out);
    } else {
        constexpr std::size_t N = sizeof...(Ts);
        auto source_kind = Backend::kind_of(src);

        auto [live, live_count] = detail::build_live_mask<Ts...>(source_kind);
        if(live_count == 0)
            return Backend::type_mismatch;

        // For a single candidate, try directly (no re-parse needed).
        if(live_count == 1) {
            auto idx = static_cast<std::size_t>(std::countr_zero(live));
            return deserialize_variant_at_v2<Backend>(idx, src, out);
        }

        // Capture raw JSON before scoring consumes the value.
        auto [raw_json, cap_err] = Backend::capture_raw_json(src);
        if(cap_err != Backend::success)
            return cap_err;

        // Score by re-parsing the raw JSON.
        std::size_t scores[N] = {};
        Backend::with_reparsed(raw_json, [&](typename Backend::value_type& val) {
            auto sk = Backend::kind_of(val);
            if(meta::is_object_kind(sk) || meta::is_sequence_kind(sk)) {
                using adapter = backend_source_adapter<Backend>;
                constexpr meta::type_info_fn info_fns[] = {&meta::type_info_of<Ts, Config>...};
                detail::variant_candidate candidates[64];
                std::size_t cand_count = 0;
                std::uint64_t mask = live;
                while(mask) {
                    std::size_t idx = static_cast<std::size_t>(std::countr_zero(mask));
                    candidates[cand_count++] = {idx, &info_fns[idx]()};
                    mask &= mask - 1;
                }
                detail::multi_score<adapter>(
                    typename adapter::node_type{&val}, candidates, cand_count, scores);
            }
            return Backend::success;
        });

        // Build ranked list of alternatives by descending score.
        std::array<std::size_t, N> ranked{};
        std::size_t ranked_count = 0;
        {
            std::uint64_t mask = live;
            while(mask) {
                ranked[ranked_count++] = static_cast<std::size_t>(std::countr_zero(mask));
                mask &= mask - 1;
            }
        }
        std::sort(ranked.begin(), ranked.begin() + ranked_count,
                  [&](std::size_t a, std::size_t b) { return scores[a] > scores[b]; });

        // Try alternatives in score order, re-parsing for each attempt.
        for(std::size_t r = 0; r < ranked_count; ++r) {
            std::size_t idx = ranked[r];
            auto err = Backend::with_reparsed(
                raw_json,
                [&](typename Backend::value_type& val) -> typename Backend::error_type {
                    return deserialize_variant_at_v2<Backend>(idx, val, out);
                });
            if(err == Backend::success)
                return Backend::success;
        }
        return Backend::type_mismatch;
    }
}

}  // namespace kota::codec
