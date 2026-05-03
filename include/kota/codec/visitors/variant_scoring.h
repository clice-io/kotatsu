#pragma once

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/support/small_vector.h"
#include "kota/support/type_list.h"
#include "kota/support/type_traits.h"
#include "kota/meta/type_info.h"
#include "kota/meta/type_kind.h"

namespace kota::codec {

template <typename A>
concept source_adapter = requires(typename A::node_type node) {
    { A::kind_of(node) } -> std::same_as<meta::type_kind>;
    A::for_each_field(node, [](std::string_view, typename A::node_type) {});
    A::for_each_element(node, [](std::size_t, std::size_t, typename A::node_type) {});
};

constexpr std::size_t kind_width(meta::type_kind k) noexcept {
    switch(k) {
        case meta::type_kind::int8:
        case meta::type_kind::uint8: return 1;
        case meta::type_kind::int16:
        case meta::type_kind::uint16: return 2;
        case meta::type_kind::int32:
        case meta::type_kind::uint32:
        case meta::type_kind::float32: return 4;
        case meta::type_kind::int64:
        case meta::type_kind::uint64:
        case meta::type_kind::float64: return 8;
        default: return 0;
    }
}

constexpr bool kind_compatible(meta::type_kind target, meta::type_kind source) noexcept {
    if(target == source) {
        return true;
    }
    if(target == meta::type_kind::any || target == meta::type_kind::unknown ||
       source == meta::type_kind::any || source == meta::type_kind::unknown) {
        return true;
    }
    if(meta::is_integer_kind(target) && meta::is_integer_kind(source)) {
        return true;
    }
    if(meta::is_floating_kind(target) &&
       (meta::is_integer_kind(source) || meta::is_floating_kind(source))) {
        return true;
    }
    if((target == meta::type_kind::string || target == meta::type_kind::character) &&
       (source == meta::type_kind::string || source == meta::type_kind::character)) {
        return true;
    }
    if(meta::is_object_kind(target) && meta::is_object_kind(source)) {
        return true;
    }
    if(meta::is_sequence_kind(target) && meta::is_sequence_kind(source)) {
        return true;
    }
    if(target == meta::type_kind::bytes &&
       (meta::is_sequence_kind(source) || source == meta::type_kind::string)) {
        return true;
    }
    if(target == meta::type_kind::enumeration &&
       (meta::is_integer_kind(source) || source == meta::type_kind::string)) {
        return true;
    }
    return false;
}

constexpr std::size_t kind_match_quality(meta::type_kind target, meta::type_kind source) noexcept {
    if(target == source)
        return 3;
    if(target == meta::type_kind::any || target == meta::type_kind::unknown ||
       source == meta::type_kind::any || source == meta::type_kind::unknown)
        return 1;
    if(meta::is_integer_kind(target) && meta::is_integer_kind(source))
        return 2;
    if(meta::is_floating_kind(target) && meta::is_floating_kind(source))
        return 2;
    if(meta::is_floating_kind(target) && meta::is_integer_kind(source))
        return 1;
    if((target == meta::type_kind::string || target == meta::type_kind::character) &&
       (source == meta::type_kind::string || source == meta::type_kind::character))
        return 2;
    if(meta::is_object_kind(target) && meta::is_object_kind(source))
        return 2;
    if(meta::is_sequence_kind(target) && meta::is_sequence_kind(source))
        return 2;
    if(target == meta::type_kind::bytes &&
       (meta::is_sequence_kind(source) || source == meta::type_kind::string))
        return 1;
    if(target == meta::type_kind::enumeration &&
       (meta::is_integer_kind(source) || source == meta::type_kind::string))
        return 1;
    return 0;
}

// accepts_kind mirrors score_type_info at compile time: kind_compatible(k, source) ↔
// kind_match_quality(k, source) > 0. Both unwrap optional/pointer/variant independently because
// accepts_kind operates on static types while score_type_info uses runtime type_info.
template <typename T>
constexpr bool accepts_kind(meta::type_kind source) noexcept {
    constexpr auto k = meta::kind_of<T>();

    if constexpr(k == meta::type_kind::optional) {
        return source == meta::type_kind::null || accepts_kind<typename T::value_type>(source);
    } else if constexpr(k == meta::type_kind::pointer) {
        return source == meta::type_kind::null || accepts_kind<typename T::element_type>(source);
    } else if constexpr(k == meta::type_kind::variant) {
        return [source]<std::size_t... I>(std::index_sequence<I...>) {
            return (accepts_kind<std::variant_alternative_t<I, T>>(source) || ...);
        }(std::make_index_sequence<std::variant_size_v<T>>{});
    } else {
        return kind_compatible(k, source);
    }
}

namespace detail {

struct variant_candidate {
    std::size_t index;
    const meta::type_info* type;
};

// Both optional and pointer kinds use optional_type_info in type_instance_impl.
const inline meta::type_info* unwrap_indirect(const meta::type_info* info) {
    while(info->kind == meta::type_kind::optional || info->kind == meta::type_kind::pointer) {
        info = &static_cast<const meta::optional_type_info&>(*info).inner();
    }
    return info;
}

// Separates quality (0-3) from width (0-8) into distinct score bands.
constexpr std::size_t quality_scale = 16;

inline std::size_t score_type_info(const meta::type_info* info, meta::type_kind source_kind) {
    if(info->kind == meta::type_kind::optional || info->kind == meta::type_kind::pointer) {
        if(source_kind == meta::type_kind::null)
            return 3 * quality_scale;
        auto& oi = static_cast<const meta::optional_type_info&>(*info);
        return score_type_info(&oi.inner(), source_kind);
    }
    if(info->kind == meta::type_kind::variant) {
        auto& vi = static_cast<const meta::variant_type_info&>(*info);
        std::size_t best = 0;
        for(const auto& alt_fn: vi.alternatives) {
            best = std::max(best, score_type_info(&alt_fn(), source_kind));
        }
        return best;
    }
    auto q = kind_match_quality(info->kind, source_kind);
    return q * quality_scale + kind_width(info->kind);
}

template <typename F>
bool field_name_matches(const F& field, std::string_view name) {
    return field.name == name || std::ranges::contains(field.aliases, name);
}

template <source_adapter Adapter>
void multi_score(typename Adapter::node_type node,
                 const variant_candidate* candidates,
                 std::size_t count,
                 std::size_t* scores) {
    if(count == 0)
        return;

    auto source_kind = Adapter::kind_of(node);

    small_vector<variant_candidate, 4> struct_cands;
    small_vector<variant_candidate, 4> map_cands;
    small_vector<variant_candidate, 4> seq_cands;

    struct expansion_group {
        std::size_t parent_index;
        std::size_t start;
        std::size_t count;
    };

    small_vector<variant_candidate, 4> expanded;
    small_vector<expansion_group, 4> groups;
    for(std::size_t i = 0; i < count; ++i) {
        const auto* info = unwrap_indirect(candidates[i].type);
        if(info->kind == meta::type_kind::variant) {
            auto& vi = static_cast<const meta::variant_type_info&>(*info);
            std::size_t start = expanded.size();
            for(const auto& alt_fn: vi.alternatives) {
                // index into exp_scores[], not into the outer scores[]
                expanded.push_back({expanded.size(), &alt_fn()});
            }
            groups.push_back({candidates[i].index, start, expanded.size() - start});
        }
    }
    if(!expanded.empty()) {
        small_vector<std::size_t, 4> exp_scores(expanded.size());
        multi_score<Adapter>(node, expanded.data(), expanded.size(), exp_scores.data());
        for(auto& group: groups) {
            std::size_t best = 0;
            for(std::size_t j = group.start; j < group.start + group.count; ++j) {
                best = std::max(best, exp_scores[j]);
            }
            scores[group.parent_index] += best;
        }
    }

    for(std::size_t i = 0; i < count; ++i) {
        const auto* info = unwrap_indirect(candidates[i].type);
        if(info->kind == meta::type_kind::variant)
            continue;
        if(!kind_compatible(info->kind, source_kind))
            continue;

        scores[candidates[i].index] += 1;

        if(info->kind == meta::type_kind::structure)
            struct_cands.push_back({candidates[i].index, info});
        else if(info->kind == meta::type_kind::map)
            map_cands.push_back({candidates[i].index, info});
        else if(meta::is_sequence_kind(info->kind))
            seq_cands.push_back({candidates[i].index, info});
    }

    if(meta::is_object_kind(source_kind) && (struct_cands.size() + map_cands.size()) > 0) {
        small_vector<std::uint64_t, 4> matched_fields(struct_cands.size());
        Adapter::for_each_field(
            node,
            [&](std::string_view name, typename Adapter::node_type child) {
                small_vector<variant_candidate, 4> child_cands;

                for(std::size_t si = 0; si < struct_cands.size(); ++si) {
                    auto& sc = struct_cands[si];
                    auto& info = static_cast<const meta::struct_type_info&>(*sc.type);
                    for(std::size_t fi = 0; fi < info.fields.size(); ++fi) {
                        if(field_name_matches(info.fields[fi], name)) {
                            child_cands.push_back({sc.index, &info.fields[fi].type()});
                            if(fi < 64)
                                matched_fields[si] |= std::uint64_t{1} << fi;
                            break;
                        }
                    }
                }

                for(auto& mc: map_cands) {
                    auto& mi = static_cast<const meta::map_type_info&>(*mc.type);
                    child_cands.push_back({mc.index, &mi.value()});
                }

                if(!child_cands.empty()) {
                    multi_score<Adapter>(child, child_cands.data(), child_cands.size(), scores);
                }
            });

        for(std::size_t si = 0; si < struct_cands.size(); ++si) {
            if(matched_fields[si] == 0) {
                auto& info = static_cast<const meta::struct_type_info&>(*struct_cands[si].type);
                if(!info.fields.empty() && scores[struct_cands[si].index] > 0)
                    scores[struct_cands[si].index] -= 1;
                continue;
            }

            auto& info = static_cast<const meta::struct_type_info&>(*struct_cands[si].type);
            for(std::size_t fi = 0; fi < info.fields.size(); ++fi) {
                auto& f = info.fields[fi];
                if(f.has_default || f.has_skip_if)
                    continue;
                if(fi >= 64 || !(matched_fields[si] & (std::uint64_t{1} << fi))) {
                    scores[struct_cands[si].index] = 0;
                    break;
                }
            }
        }
    }

    if(meta::is_sequence_kind(source_kind) && !seq_cands.empty()) {
        Adapter::for_each_element(
            node,
            [&](std::size_t elem_idx, std::size_t total, typename Adapter::node_type elem) {
                small_vector<variant_candidate, 4> child_cands;

                for(auto& sc: seq_cands) {
                    const auto* info = sc.type;
                    if(info->kind == meta::type_kind::array || info->kind == meta::type_kind::set) {
                        auto& ai = static_cast<const meta::array_type_info&>(*info);
                        child_cands.push_back({sc.index, &ai.element()});
                    } else if(info->kind == meta::type_kind::tuple) {
                        auto& ti = static_cast<const meta::tuple_type_info&>(*info);
                        if(ti.elements.size() != total)
                            continue;
                        if(elem_idx < ti.elements.size()) {
                            child_cands.push_back({sc.index, &ti.elements[elem_idx]()});
                        }
                    }
                }

                if(!child_cands.empty()) {
                    multi_score<Adapter>(elem, child_cands.data(), child_cands.size(), scores);
                }
            });
    }
}

struct live_mask {
    std::uint64_t bits;
    std::size_t count;
};

template <typename... Ts>
constexpr live_mask build_live_mask(meta::type_kind source_kind) {
    static_assert(sizeof...(Ts) <= 64, "variant with more than 64 alternatives is not supported");
    std::uint64_t bits = 0;
    std::size_t count = 0;
    std::size_t idx = 0;
    (([&] {
         if(accepts_kind<Ts>(source_kind)) {
             bits |= (std::uint64_t{1} << idx);
             ++count;
         }
         ++idx;
     }()),
     ...);
    return {bits, count};
}

template <typename Config, typename... Ts>
std::optional<std::size_t> select_by_kind(std::uint64_t live, meta::type_kind source_kind) {
    constexpr meta::type_info_fn info_fns[] = {&meta::type_info_of<Ts, Config>...};
    std::size_t best_score = 0;
    std::optional<std::size_t> best_idx;
    std::uint64_t mask = live;
    while(mask) {
        std::size_t idx = static_cast<std::size_t>(std::countr_zero(mask));
        auto s = score_type_info(&info_fns[idx](), source_kind);
        if(s > best_score) {
            best_score = s;
            best_idx = idx;
        }
        mask &= mask - 1;
    }
    return best_idx;
}

}  // namespace detail

template <typename Config, typename... Ts>
std::optional<std::size_t> select_variant_index(meta::type_kind source_kind) {
    static_assert(sizeof...(Ts) <= 64, "variant with more than 64 alternatives is not supported");

    auto [live, live_count] = detail::build_live_mask<Ts...>(source_kind);

    if(live_count == 0)
        return std::nullopt;
    if(live_count == 1)
        return static_cast<std::size_t>(std::countr_zero(live));

    return detail::select_by_kind<Config, Ts...>(live, source_kind);
}

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

/// select_variant_index: uses Backend for scoring instead of source_adapter
template <typename Backend, typename Config, typename... Ts>
auto select_variant_index(typename Backend::value_type& src) -> std::optional<std::size_t> {
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

        detail::multi_score<adapter>(typename adapter::node_type{&src},
                                     candidates,
                                     cand_count,
                                     scores);

        std::size_t best_score = 0;
        std::optional<std::size_t> best_idx;
        {
            std::uint64_t mask = live;
            while(mask) {
                std::size_t idx = static_cast<std::size_t>(std::countr_zero(mask));
                if(scores[idx] > best_score) {
                    best_score = scores[idx];
                    best_idx = idx;
                }
                mask &= mask - 1;
            }
        }

        if(best_idx && best_score > 0)
            return best_idx;
    }

    // Fall back to kind-based selection
    return detail::select_by_kind<Config, Ts...>(live, source_kind);
}

/// deserialize_variant_at: deserialize into variant at index
template <typename Backend, typename... Ts>
auto deserialize_variant_at(std::size_t idx,
                            typename Backend::value_type& src,
                            std::variant<Ts...>& out) -> typename Backend::error_type {
    using E = typename Backend::error_type;
    E err = Backend::type_mismatch;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (void)((Is == idx ? (
                                err =
                                    [&] {
                                        using alt_t =
                                            std::variant_alternative_t<Is, std::variant<Ts...>>;
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

/// deserialize_variant_untagged: select best alternative then deserialize.
/// Scoring uses visit_object_keys / visit_array_keys which internally reset
/// the iterator, so the source remains consumable for deserialization.
template <typename Backend, typename Config, typename... Ts>
auto deserialize_variant_untagged(typename Backend::value_type& src, std::variant<Ts...>& out) ->
    typename Backend::error_type {
    auto idx = select_variant_index<Backend, Config, Ts...>(src);
    if(!idx)
        return Backend::type_mismatch;
    return deserialize_variant_at<Backend>(*idx, src, out);
}

}  // namespace kota::codec
