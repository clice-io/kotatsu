#pragma once

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/support/expected_try.h"
#include "kota/support/small_vector.h"
#include "kota/support/type_list.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/meta/struct.h"
#include "kota/codec/content/document.h"
#include "kota/codec/detail/common.h"
#include "kota/codec/detail/config.h"
#include "kota/codec/detail/struct_serialize.h"

namespace kota::codec::detail {

template <typename E, typename... Ts, typename Names, typename Reader>
constexpr auto match_and_deserialize_alt(std::string_view tag_value,
                                         const Names& names,
                                         std::variant<Ts...>& value,
                                         Reader&& reader) -> std::expected<void, E> {
    bool matched = false;
    std::expected<void, E> status{};

    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (([&] {
             if(matched || names[I] != tag_value) {
                 return;
             }
             matched = true;

             using alt_t = std::variant_alternative_t<I, std::variant<Ts...>>;
             if constexpr(std::same_as<alt_t, std::monostate>) {
                 std::monostate alt{};
                 auto result = reader(alt);
                 if(!result) {
                     status = std::unexpected(result.error());
                 } else {
                     value.template emplace<I>();
                 }
             } else if constexpr(std::default_initializable<alt_t>) {
                 alt_t alt{};
                 auto result = reader(alt);
                 if(!result) {
                     status = std::unexpected(result.error());
                 } else {
                     value = std::move(alt);
                 }
             } else {
                 status = std::unexpected(E::invalid_state);
             }
         }()),
         ...);
    }(std::make_index_sequence<sizeof...(Ts)>{});

    if(!matched) {
        return std::unexpected(E::custom(std::format("unknown variant tag '{}'", tag_value)));
    }
    return status;
}

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_externally_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E> {
    constexpr auto names = meta::resolve_tag_names<TagAttr, Ts...>();

    KOTA_EXPECTED_TRY(s.begin_object(1));

    auto name = names[value.index()];

    std::expected<void, E> inner_status{};
    std::visit(
        [&](const auto& item) {
            auto r = s.serialize_field(name, [&] { return codec::serialize(s, item); });
            if(!r) {
                inner_status = std::unexpected(r.error());
            }
        },
        value);
    if(!inner_status) {
        return std::unexpected(inner_status.error());
    }

    return s.end_object();
}

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_adjacently_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E> {
    constexpr auto names = meta::resolve_tag_names<TagAttr, Ts...>();

    KOTA_EXPECTED_TRY(s.begin_object(2));

    // Tag field
    auto tag_name = names[value.index()];
    KOTA_EXPECTED_TRY(
        s.serialize_field(TagAttr::field_names[0], [&] { return codec::serialize(s, tag_name); }));

    // Content field
    std::expected<void, E> inner_status{};
    std::visit(
        [&](const auto& item) {
            auto r = s.serialize_field(TagAttr::field_names[1],
                                       [&] { return codec::serialize(s, item); });
            if(!r) {
                inner_status = std::unexpected(r.error());
            }
        },
        value);
    if(!inner_status) {
        return std::unexpected(inner_status.error());
    }

    return s.end_object();
}

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_internally_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E> {
    constexpr auto names = meta::resolve_tag_names<TagAttr, Ts...>();
    constexpr std::string_view tag_field = TagAttr::field_names[0];

    return std::visit(
        [&](const auto& item) -> std::expected<typename S::value_type, E> {
            using alt_t = std::remove_cvref_t<decltype(item)>;
            static_assert(meta::reflectable_class<alt_t>,
                          "internally_tagged requires struct alternatives");

            using config_t = config::config_of<S>;
            using schema = meta::virtual_schema<alt_t, config_t>;
            using slots = typename schema::slots;
            constexpr std::size_t N = type_list_size_v<slots>;

            KOTA_EXPECTED_TRY(s.begin_object(N + 1));

            // Tag field first
            auto tag_name = names[value.index()];
            KOTA_EXPECTED_TRY(
                s.serialize_field(tag_field, [&] { return codec::serialize(s, tag_name); }));

            // Struct fields via schema
            serialize_by_name_visitor<E, S> visitor{s};
            KOTA_EXPECTED_TRY((for_each_field<config_t, true>(item, visitor)));
            return s.end_object();
        },
        value);
}

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_externally_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E> {
    constexpr auto names = meta::resolve_tag_names<TagAttr, Ts...>();

    KOTA_EXPECTED_TRY(d.begin_object());

    KOTA_EXPECTED_TRY_V(auto key, d.next_field());
    if(!key.has_value()) {
        return std::unexpected(E::custom("expected externally tagged variant key"));
    }

    KOTA_EXPECTED_TRY((match_and_deserialize_alt<E>(*key, names, value, [&](auto& alt) {
        return codec::deserialize(d, alt);
    })));

    // Reject trailing fields — externally tagged must have exactly one key.
    KOTA_EXPECTED_TRY_V(auto trailing, d.next_field());
    if(trailing.has_value()) {
        return std::unexpected(E::custom("externally tagged variant must have exactly one field"));
    }

    return d.end_object();
}

template <typename D>
concept can_buffer_raw_field = requires(D& d) {
    { d.buffer_raw_field_value() } -> std::same_as<typename D::template result_t<std::string>>;
};

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_adjacently_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E> {
    constexpr auto names = meta::resolve_tag_names<TagAttr, Ts...>();

    KOTA_EXPECTED_TRY(d.begin_object());

    std::string tag_value;

    auto deserialize_content_for_tag = [&](auto&& read_content_alt) -> std::expected<void, E> {
        return match_and_deserialize_alt<E>(
            tag_value,
            names,
            value,
            std::forward<decltype(read_content_alt)>(read_content_alt));
    };

    auto read_content_direct = [&](auto& alt) -> std::expected<void, E> {
        return codec::deserialize(d, alt);
    };

    if constexpr(can_buffer_raw_field<D>) {
        std::optional<std::string> buffered_raw;
        bool has_tag = false;
        bool has_content = false;

        while(true) {
            KOTA_EXPECTED_TRY_V(auto field_key, d.next_field());
            if(!field_key.has_value()) {
                break;
            }

            if(*field_key == TagAttr::field_names[0]) {
                if(has_tag) {
                    return std::unexpected(E::duplicate_field(TagAttr::field_names[0]));
                }
                KOTA_EXPECTED_TRY(codec::deserialize(d, tag_value));
                has_tag = true;
            } else if(*field_key == TagAttr::field_names[1]) {
                if(has_content) {
                    return std::unexpected(E::duplicate_field(TagAttr::field_names[1]));
                }
                has_content = true;

                if(has_tag) {
                    KOTA_EXPECTED_TRY(deserialize_content_for_tag(read_content_direct));
                } else {
                    KOTA_EXPECTED_TRY_V(auto raw, d.buffer_raw_field_value());
                    buffered_raw.emplace(std::move(raw));
                }
            } else {
                KOTA_EXPECTED_TRY(d.skip_field_value());
            }
        }

        if(!has_tag || !has_content) {
            if(!has_tag) {
                return std::unexpected(E::missing_field(TagAttr::field_names[0]));
            }
            return std::unexpected(E::missing_field(TagAttr::field_names[1]));
        }

        if(buffered_raw.has_value()) {
            KOTA_EXPECTED_TRY(deserialize_content_for_tag([&](auto& alt) -> std::expected<void, E> {
                return d.replay_buffered_field(*buffered_raw, alt);
            }));
        }

        return d.end_object();
    } else {
        // Strict order: tag then content
        KOTA_EXPECTED_TRY_V(auto key1, d.next_field());
        if(!key1.has_value() || *key1 != TagAttr::field_names[0]) {
            return std::unexpected(E::custom(
                std::format("expected adjacent tag field '{}'", TagAttr::field_names[0])));
        }
        KOTA_EXPECTED_TRY(codec::deserialize(d, tag_value));

        KOTA_EXPECTED_TRY_V(auto key2, d.next_field());
        if(!key2.has_value() || *key2 != TagAttr::field_names[1]) {
            return std::unexpected(E::custom(
                std::format("expected adjacent content field '{}'", TagAttr::field_names[1])));
        }
        KOTA_EXPECTED_TRY(deserialize_content_for_tag(read_content_direct));

        return d.end_object();
    }
}

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_internally_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E> {
    constexpr auto names = meta::resolve_tag_names<TagAttr, Ts...>();
    constexpr std::string_view tag_field = TagAttr::field_names[0];

    KOTA_EXPECTED_TRY_V(auto tag_value, d.scan_object_field(tag_field));

    return match_and_deserialize_alt<E>(tag_value,
                                        names,
                                        value,
                                        [&](auto& alt) -> std::expected<void, E> {
                                            using alt_t = std::remove_cvref_t<decltype(alt)>;
                                            static_assert(
                                                meta::reflectable_class<alt_t>,
                                                "internally_tagged requires struct alternatives");
                                            return codec::deserialize(d, alt);
                                        });
}

}  // namespace kota::codec::detail

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

struct content_source_adapter {
    using node_type = const content::Value*;

    static meta::type_kind kind_of(node_type node) {
        if(!node)
            return meta::type_kind::null;
        switch(node->kind()) {
            case content::ValueKind::null_value: return meta::type_kind::null;
            case content::ValueKind::boolean: return meta::type_kind::boolean;
            case content::ValueKind::signed_int: return meta::type_kind::int64;
            case content::ValueKind::unsigned_int: return meta::type_kind::uint64;
            case content::ValueKind::floating: return meta::type_kind::float64;
            case content::ValueKind::string: return meta::type_kind::string;
            case content::ValueKind::array: return meta::type_kind::array;
            case content::ValueKind::object: return meta::type_kind::structure;
            default: return meta::type_kind::unknown;
        }
    }

    template <typename Fn>
    static void for_each_field(node_type node, Fn&& fn) {
        if(!node)
            return;
        if(const auto* obj = node->get_object()) {
            for(const auto& entry: *obj) {
                fn(std::string_view(entry.key), &entry.value);
            }
        }
    }

    template <typename Fn>
    static void for_each_element(node_type node, Fn&& fn) {
        if(!node)
            return;
        if(const auto* arr = node->get_array()) {
            std::size_t total = arr->size();
            for(std::size_t i = 0; i < total; ++i) {
                fn(i, total, &(*arr)[i]);
            }
        }
    }
};

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
        small_vector<bool, 4> struct_matched(struct_cands.size());
        small_vector<std::string_view, 8> source_fields;
        Adapter::for_each_field(
            node,
            [&](std::string_view name, typename Adapter::node_type child) {
                source_fields.push_back(name);
                small_vector<variant_candidate, 4> child_cands;

                for(std::size_t si = 0; si < struct_cands.size(); ++si) {
                    auto& sc = struct_cands[si];
                    auto& info = static_cast<const meta::struct_type_info&>(*sc.type);
                    for(const auto& f: info.fields) {
                        if(field_name_matches(f, name)) {
                            child_cands.push_back({sc.index, &f.type()});
                            struct_matched[si] = true;
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
            if(!struct_matched[si]) {
                auto& info = static_cast<const meta::struct_type_info&>(*struct_cands[si].type);
                if(!info.fields.empty() && scores[struct_cands[si].index] > 0)
                    scores[struct_cands[si].index] -= 1;
                continue;
            }

            auto& info = static_cast<const meta::struct_type_info&>(*struct_cands[si].type);
            for(const auto& f: info.fields) {
                if(f.has_default || f.has_skip_if)
                    continue;
                bool present = std::ranges::any_of(source_fields, [&](std::string_view name) {
                    return field_name_matches(f, name);
                });
                if(!present && scores[struct_cands[si].index] > 0)
                    scores[struct_cands[si].index] -= 1;
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

template <source_adapter Adapter, typename Config, typename... Ts>
std::optional<std::size_t> select_variant_index(typename Adapter::node_type node) {
    constexpr std::size_t N = sizeof...(Ts);
    static_assert(N <= 64, "variant with more than 64 alternatives is not supported");

    auto source_kind = Adapter::kind_of(node);

    auto [live, live_count] = detail::build_live_mask<Ts...>(source_kind);

    if(live_count == 0)
        return std::nullopt;
    if(live_count == 1)
        return static_cast<std::size_t>(std::countr_zero(live));

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

    detail::multi_score<Adapter>(node, candidates, cand_count, scores);

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

    // Tied deep scores: fall back to kind_match_quality which prefers exact kind
    // matches.  When struct and map tie (e.g. identical field types), declaration
    // order among equal-quality candidates decides the winner.
    return detail::select_by_kind<Config, Ts...>(live, source_kind);
}

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

template <typename E, typename D, typename... Ts>
auto deserialize_variant_at(D& d, std::variant<Ts...>& value, std::size_t best)
    -> std::expected<void, E> {
    std::expected<void, E> result = std::unexpected(E::type_mismatch);
    std::size_t idx = 0;
    auto try_alt = [&](auto type_tag) {
        if(idx++ != best)
            return;
        using alt_t = typename decltype(type_tag)::type;
        alt_t candidate{};
        auto status = codec::deserialize(d, candidate);
        if(status) {
            value = std::move(candidate);
            result = {};
        } else {
            result = std::unexpected(status.error());
        }
    };
    (try_alt(std::type_identity<Ts>{}), ...);
    return result;
}

}  // namespace kota::codec
