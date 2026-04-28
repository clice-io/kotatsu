#pragma once

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
    using U = std::remove_cvref_t<T>;
    constexpr auto k = meta::kind_of<U>();

    if constexpr(k == meta::type_kind::optional) {
        return source == meta::type_kind::null || accepts_kind<typename U::value_type>(source);
    } else if constexpr(k == meta::type_kind::pointer) {
        return source == meta::type_kind::null || accepts_kind<typename U::element_type>(source);
    } else if constexpr(k == meta::type_kind::variant) {
        return true;
    } else {
        return kind_compatible(k, source);
    }
}

namespace detail {

template <typename T, typename Config>
bool alt_has_field(std::string_view name) {
    using U = std::remove_cvref_t<T>;
    constexpr auto k = meta::kind_of<U>();

    if constexpr(k == meta::type_kind::optional) {
        return alt_has_field<typename U::value_type, Config>(name);
    } else if constexpr(k == meta::type_kind::pointer) {
        return alt_has_field<typename U::element_type, Config>(name);
    } else if constexpr(!meta::reflectable_class<U>) {
        return true;
    } else if constexpr(meta::field_count<U>() == 0) {
        return true;
    } else {
        using schema = meta::virtual_schema<U, Config>;
        if constexpr(!schema::deny_unknown) {
            return true;
        }
        for(const auto& field: schema::fields) {
            if(field.name == name) {
                return true;
            }
            for(auto alias: field.aliases) {
                if(alias == name) {
                    return true;
                }
            }
        }
        return false;
    }
}

template <typename Config, typename... Ts>
struct alt_has_field_table {
    using fn_t = bool (*)(std::string_view);
    constexpr static fn_t table[] = {alt_has_field<Ts, Config>...};
};

template <typename T>
consteval meta::type_kind resolved_alt_kind() noexcept {
    using U = std::remove_cvref_t<T>;
    constexpr auto k = meta::kind_of<U>();
    if constexpr(k == meta::type_kind::optional) {
        return resolved_alt_kind<typename U::value_type>();
    } else if constexpr(k == meta::type_kind::pointer) {
        return resolved_alt_kind<typename U::element_type>();
    } else {
        return k;
    }
}

}  // namespace detail

template <typename Config, typename... Ts, typename Visitor>
std::size_t decide_variant_index(meta::type_kind source_kind, Visitor&& visit) {
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

    if(live_count > 1 && meta::is_object_kind(source_kind)) {
        using table = detail::alt_has_field_table<Config, Ts...>;
        std::forward<Visitor>(visit)([&](std::string_view field_name) -> bool {
            std::uint64_t mask = live;
            while(mask) {
                std::size_t idx = static_cast<std::size_t>(__builtin_ctzll(mask));
                if(!table::table[idx](field_name)) {
                    live &= ~(std::uint64_t{1} << idx);
                    --live_count;
                }
                mask &= mask - 1;
            }
            return live_count <= 1;
        });
    }

    if(live_count > 1) {
        constexpr meta::type_kind alt_kinds[] = {detail::resolved_alt_kind<Ts>()...};
        bool all_integer = true;
        bool all_float = true;
        {
            std::uint64_t mask = live;
            while(mask) {
                std::size_t idx = static_cast<std::size_t>(__builtin_ctzll(mask));
                auto ak = alt_kinds[idx];
                if(!meta::is_integer_kind(ak)) {
                    all_integer = false;
                }
                if(!meta::is_floating_kind(ak)) {
                    all_float = false;
                }
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
    if(live_count == 1) {
        return static_cast<std::size_t>(__builtin_ctzll(live));
    }
    return N;
}

template <typename Config, typename... Ts>
std::size_t decide_variant_index(meta::type_kind source_kind) {
    return decide_variant_index<Config, Ts...>(source_kind, [](auto&&) {});
}

template <typename D, typename Alt, typename Source, typename... Ts>
auto try_deserialize_variant_candidate(Source&& source, std::variant<Ts...>& value)
    -> std::expected<void, typename D::error_type> {
    Alt candidate{};
    D probe(std::forward<Source>(source));
    if(!probe.valid()) {
        return std::unexpected(probe.error());
    }

    auto status = codec::deserialize(probe, candidate);
    if(!status) {
        return std::unexpected(status.error());
    }

    auto finished = probe.finish();
    if(!finished) {
        return std::unexpected(finished.error());
    }

    value = std::move(candidate);
    return {};
}

template <typename D, typename Source, typename Visitor, typename... Ts>
auto try_variant_dispatch(Source&& source,
                          meta::type_kind source_kind,
                          std::variant<Ts...>& value,
                          typename D::error_type mismatch_error,
                          Visitor&& visit) -> std::expected<void, typename D::error_type> {
    static_assert((std::default_initializable<Ts> && ...),
                  "variant deserialization requires default-constructible alternatives");

    using error_type = typename D::error_type;
    using config_t = typename D::config_type;
    constexpr std::size_t N = sizeof...(Ts);

    std::size_t best =
        decide_variant_index<config_t, Ts...>(source_kind, std::forward<Visitor>(visit));

    if(best >= N) {
        return std::unexpected(mismatch_error);
    }

    bool matched = false;
    error_type last_error = mismatch_error;

    auto try_at = [&](auto type_tag) {
        if(matched) {
            return;
        }
        using alt_t = typename decltype(type_tag)::type;
        auto status =
            try_deserialize_variant_candidate<D, alt_t>(std::forward<Source>(source), value);
        if(status) {
            matched = true;
        } else {
            last_error = status.error();
        }
    };

    {
        std::size_t idx = 0;
        auto try_best = [&](auto type_tag) {
            if(idx++ == best) {
                try_at(type_tag);
            }
        };
        (try_best(std::type_identity<Ts>{}), ...);
    }

    if(!matched) {
        std::size_t idx = 0;
        auto try_fallback = [&](auto type_tag) {
            std::size_t current = idx++;
            if(matched || current == best) {
                return;
            }
            using alt_t = typename decltype(type_tag)::type;
            if(!accepts_kind<alt_t>(source_kind)) {
                return;
            }
            try_at(type_tag);
        };
        (try_fallback(std::type_identity<Ts>{}), ...);
    }

    if(!matched) {
        return std::unexpected(last_error);
    }
    return {};
}

template <typename D, typename Source, typename... Ts>
auto try_variant_dispatch(Source&& source,
                          meta::type_kind source_kind,
                          std::variant<Ts...>& value,
                          typename D::error_type mismatch_error)
    -> std::expected<void, typename D::error_type> {
    return try_variant_dispatch<D>(std::forward<Source>(source),
                                   source_kind,
                                   value,
                                   mismatch_error,
                                   [](auto&&) {});
}

}  // namespace kota::codec
