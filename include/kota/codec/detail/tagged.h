#pragma once

#include <algorithm>
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

    if constexpr(detail::can_buffer_adjacently_tagged_v<D>) {
        using captured_t = detail::captured_dom_value_t<D>;
        std::optional<captured_t> buffered_content;
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
                    captured_t captured{};
                    KOTA_EXPECTED_TRY(codec::deserialize(d, captured));
                    buffered_content.emplace(std::move(captured));
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

        if(buffered_content.has_value()) {
            KOTA_EXPECTED_TRY(deserialize_content_for_tag([&](auto& alt) -> std::expected<void, E> {
                content::Deserializer<typename D::config_type> buffered_deserializer(
                    *buffered_content);
                KOTA_EXPECTED_TRY(codec::deserialize(buffered_deserializer, alt));
                KOTA_EXPECTED_TRY(buffered_deserializer.finish());
                return {};
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
    using config_t = config::config_of<D>;

    // Requires capture_dom_value() — buffer to content DOM, then two-pass dispatch
    KOTA_EXPECTED_TRY_V(auto dom_result, d.capture_dom_value());

    constexpr auto names = meta::resolve_tag_names<TagAttr, Ts...>();
    constexpr std::string_view tag_field = TagAttr::field_names[0];

    auto dom_cursor = dom_result.cursor();
    const content::Object* obj = dom_cursor.get_object();
    if(obj == nullptr) {
        return std::unexpected(E::invalid_type("object", "non-object"));
    }

    // Pass 1: find tag
    std::string_view tag_value;
    bool found = false;
    for(const auto& entry: *obj) {
        if(entry.key == tag_field) {
            auto s = entry.value.get_string();
            if(!s) {
                return std::unexpected(E::invalid_type("string", "non-string"));
            }
            tag_value = *s;
            found = true;
            break;
        }
    }
    if(!found) {
        return std::unexpected(E::missing_field(tag_field));
    }

    // Pass 2: match tag -> deserialize full object as that struct type
    return match_and_deserialize_alt<E>(tag_value,
                                        names,
                                        value,
                                        [&](auto& alt) -> std::expected<void, E> {
                                            using alt_t = std::remove_cvref_t<decltype(alt)>;
                                            static_assert(
                                                meta::reflectable_class<alt_t>,
                                                "internally_tagged requires struct alternatives");

                                            content::Deserializer<config_t> deser(dom_cursor);
                                            KOTA_EXPECTED_TRY(codec::deserialize(deser, alt));
                                            KOTA_EXPECTED_TRY(deser.finish());
                                            return {};
                                        });
}

}  // namespace kota::codec::detail

namespace kota::codec {

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

template <typename T>
constexpr bool accepts_kind(meta::type_kind source) noexcept {
    constexpr auto k = meta::kind_of<T>();

    if constexpr(k == meta::type_kind::optional) {
        return source == meta::type_kind::null || accepts_kind<typename T::value_type>(source);
    } else if constexpr(k == meta::type_kind::pointer) {
        return source == meta::type_kind::null || accepts_kind<typename T::element_type>(source);
    } else if constexpr(k == meta::type_kind::variant) {
        return true;
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
            case content::ValueKind::signed_int:
            case content::ValueKind::unsigned_int: return meta::type_kind::int64;
            case content::ValueKind::floating: return meta::type_kind::float64;
            case content::ValueKind::string: return meta::type_kind::string;
            case content::ValueKind::array: return meta::type_kind::array;
            case content::ValueKind::object: return meta::type_kind::structure;
        }
        return meta::type_kind::any;
    }

    template <typename Fn>
    static void for_each_field(node_type node, Fn&& fn) {
        if(const auto* obj = node->get_object()) {
            for(const auto& entry: *obj) {
                fn(std::string_view(entry.key), &entry.value);
            }
        }
    }

    template <typename Fn>
    static void for_each_element(node_type node, Fn&& fn) {
        if(const auto* arr = node->get_array()) {
            std::size_t total = arr->size();
            for(std::size_t i = 0; i < total; ++i) {
                fn(i, total, &(*arr)[i]);
            }
        }
    }
};

namespace detail {

template <typename T>
consteval meta::type_kind resolved_alt_kind() noexcept {
    constexpr auto k = meta::kind_of<T>();
    if constexpr(k == meta::type_kind::optional) {
        return resolved_alt_kind<typename T::value_type>();
    } else if constexpr(k == meta::type_kind::pointer) {
        return resolved_alt_kind<typename T::element_type>();
    } else {
        return k;
    }
}

struct variant_candidate {
    std::size_t index;
    const meta::type_info* type;
};

const inline meta::type_info* unwrap_indirect(const meta::type_info* info) {
    while(info->kind == meta::type_kind::optional || info->kind == meta::type_kind::pointer) {
        info = &static_cast<const meta::optional_type_info&>(*info).inner();
    }
    return info;
}

template <typename F>
bool field_name_matches(const F& field, std::string_view name) {
    return field.name == name || std::ranges::contains(field.aliases, name);
}

template <typename Adapter>
void multi_score(typename Adapter::node_type node,
                 const variant_candidate* candidates,
                 std::size_t count,
                 std::size_t* scores) {
    if(count == 0)
        return;

    auto source_kind = Adapter::kind_of(node);

    variant_candidate struct_cands[64];
    std::size_t struct_n = 0;
    variant_candidate map_cands[64];
    std::size_t map_n = 0;
    variant_candidate seq_cands[64];
    std::size_t seq_n = 0;

    for(std::size_t i = 0; i < count; ++i) {
        const auto* info = unwrap_indirect(candidates[i].type);
        if(!kind_compatible(info->kind, source_kind))
            continue;

        scores[candidates[i].index] += 1;

        if(info->kind == meta::type_kind::structure)
            struct_cands[struct_n++] = {candidates[i].index, info};
        else if(info->kind == meta::type_kind::map)
            map_cands[map_n++] = {candidates[i].index, info};
        else if(meta::is_sequence_kind(info->kind))
            seq_cands[seq_n++] = {candidates[i].index, info};
    }

    if(meta::is_object_kind(source_kind) && (struct_n + map_n) > 0) {
        Adapter::for_each_field(
            node,
            [&](std::string_view name, typename Adapter::node_type child) {
                variant_candidate child_cands[64];
                std::size_t child_n = 0;

                for(std::size_t i = 0; i < struct_n; ++i) {
                    auto& si = static_cast<const meta::struct_type_info&>(*struct_cands[i].type);
                    for(const auto& f: si.fields) {
                        if(field_name_matches(f, name)) {
                            child_cands[child_n++] = {struct_cands[i].index, &f.type()};
                            break;
                        }
                    }
                }

                for(std::size_t i = 0; i < map_n; ++i) {
                    auto& mi = static_cast<const meta::map_type_info&>(*map_cands[i].type);
                    child_cands[child_n++] = {map_cands[i].index, &mi.value()};
                }

                if(child_n > 0) {
                    multi_score<Adapter>(child, child_cands, child_n, scores);
                }
            });
    }

    if(meta::is_sequence_kind(source_kind) && seq_n > 0) {
        Adapter::for_each_element(
            node,
            [&](std::size_t elem_idx, std::size_t total, typename Adapter::node_type elem) {
                variant_candidate child_cands[64];
                std::size_t child_n = 0;

                for(std::size_t i = 0; i < seq_n; ++i) {
                    const auto* info = seq_cands[i].type;
                    if(info->kind == meta::type_kind::array || info->kind == meta::type_kind::set) {
                        auto& ai = static_cast<const meta::array_type_info&>(*info);
                        child_cands[child_n++] = {seq_cands[i].index, &ai.element()};
                    } else if(info->kind == meta::type_kind::tuple) {
                        auto& ti = static_cast<const meta::tuple_type_info&>(*info);
                        if(ti.elements.size() != total)
                            continue;
                        if(elem_idx < ti.elements.size()) {
                            child_cands[child_n++] = {seq_cands[i].index, &ti.elements[elem_idx]()};
                        }
                    }
                }

                if(child_n > 0) {
                    multi_score<Adapter>(elem, child_cands, child_n, scores);
                }
            });
    }
}

}  // namespace detail

template <typename... Ts>
std::size_t numeric_tiebreaker(std::uint64_t live) {
    constexpr meta::type_kind alt_kinds[] = {detail::resolved_alt_kind<Ts>()...};
    bool all_integer = true;
    bool all_float = true;
    {
        std::uint64_t mask = live;
        while(mask) {
            std::size_t idx = static_cast<std::size_t>(__builtin_ctzll(mask));
            if(!meta::is_integer_kind(alt_kinds[idx]))
                all_integer = false;
            if(!meta::is_floating_kind(alt_kinds[idx]))
                all_float = false;
            mask &= mask - 1;
        }
    }
    if(all_integer || all_float) {
        std::size_t best_idx = static_cast<std::size_t>(__builtin_ctzll(live));
        std::size_t best_width = kind_width(alt_kinds[best_idx]);
        std::uint64_t mask = live & (live - 1);
        while(mask) {
            std::size_t idx = static_cast<std::size_t>(__builtin_ctzll(mask));
            std::size_t w = kind_width(alt_kinds[idx]);
            if(w > best_width) {
                best_width = w;
                best_idx = idx;
            }
            mask &= mask - 1;
        }
        return best_idx;
    }
    return static_cast<std::size_t>(__builtin_ctzll(live));
}

template <typename Adapter, typename Config, typename... Ts>
std::size_t select_variant_index(typename Adapter::node_type node) {
    constexpr std::size_t N = sizeof...(Ts);
    static_assert(N <= 64, "variant with more than 64 alternatives is not supported");

    auto source_kind = Adapter::kind_of(node);

    std::uint64_t live = 0;
    std::size_t live_count = 0;
    {
        std::size_t idx = 0;
        auto init = [&](auto type_tag) {
            using alt_t = typename decltype(type_tag)::type;
            if(accepts_kind<alt_t>(source_kind)) {
                live |= (std::uint64_t{1} << idx);
                ++live_count;
            }
            ++idx;
        };
        (init(std::type_identity<Ts>{}), ...);
    }

    if(live_count == 0)
        return N;
    if(live_count == 1)
        return static_cast<std::size_t>(__builtin_ctzll(live));

    constexpr meta::type_info_fn info_fns[] = {&meta::type_info_of<Ts, Config>...};
    std::size_t scores[N] = {};

    detail::variant_candidate candidates[64];
    std::size_t cand_count = 0;
    {
        std::uint64_t mask = live;
        while(mask) {
            std::size_t idx = static_cast<std::size_t>(__builtin_ctzll(mask));
            candidates[cand_count++] = {idx, &info_fns[idx]()};
            mask &= mask - 1;
        }
    }

    detail::multi_score<Adapter>(node, candidates, cand_count, scores);

    std::size_t best_score = 0;
    std::size_t best_idx = N;
    {
        std::uint64_t mask = live;
        while(mask) {
            std::size_t idx = static_cast<std::size_t>(__builtin_ctzll(mask));
            if(scores[idx] > best_score) {
                best_score = scores[idx];
                best_idx = idx;
            }
            mask &= mask - 1;
        }
    }

    if(best_idx < N)
        return best_idx;

    return numeric_tiebreaker<Ts...>(live);
}

template <typename Config, typename... Ts>
std::size_t select_variant_index(meta::type_kind source_kind) {
    constexpr std::size_t N = sizeof...(Ts);
    static_assert(N <= 64, "variant with more than 64 alternatives is not supported");

    std::uint64_t live = 0;
    std::size_t live_count = 0;
    {
        std::size_t idx = 0;
        auto init = [&](auto type_tag) {
            using alt_t = typename decltype(type_tag)::type;
            if(accepts_kind<alt_t>(source_kind)) {
                live |= (std::uint64_t{1} << idx);
                ++live_count;
            }
            ++idx;
        };
        (init(std::type_identity<Ts>{}), ...);
    }

    if(live_count == 0)
        return N;
    if(live_count == 1)
        return static_cast<std::size_t>(__builtin_ctzll(live));

    return numeric_tiebreaker<Ts...>(live);
}

}  // namespace kota::codec
