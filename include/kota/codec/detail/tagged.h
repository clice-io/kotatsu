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
#include "kota/codec/config.h"
#include "kota/codec/content/document.h"
#include "kota/codec/detail/common.h"
#include "kota/codec/detail/fwd.h"
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
    KOTA_EXPECTED_TRY(s.field(name));

    std::expected<void, E> inner_status{};
    std::visit(
        [&](const auto& item) {
            auto r = emit_field_value<S, E>(s, codec::serialize(s, item));
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
    KOTA_EXPECTED_TRY(s.field(TagAttr::field_names[0]));
    {
        auto _r = emit_field_value<S, E>(s, codec::serialize(s, tag_name));
        if(!_r)
            return std::unexpected(_r.error());
    }

    // Content field
    KOTA_EXPECTED_TRY(s.field(TagAttr::field_names[1]));
    std::expected<void, E> inner_status{};
    std::visit(
        [&](const auto& item) {
            auto r = emit_field_value<S, E>(s, codec::serialize(s, item));
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
            KOTA_EXPECTED_TRY(s.field(tag_field));
            {
                auto _r = emit_field_value<S, E>(s, codec::serialize(s, tag_name));
                if(!_r)
                    return std::unexpected(_r.error());
            }

            // Struct fields via schema
            std::expected<void, E> slot_status{};
            bool ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                return ([&] {
                    auto r = serialize_slot_by_name<config_t, E, alt_t, Is>(s, item);
                    if(!r) {
                        slot_status = std::unexpected(r.error());
                        return false;
                    }
                    return true;
                }() && ...);
            }(std::make_index_sequence<N>{});

            if(!ok) {
                return std::unexpected(slot_status.error());
            }
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

enum class type_hint : std::uint8_t {
    null_like = 1 << 0,
    boolean = 1 << 1,
    integer = 1 << 2,
    floating = 1 << 3,
    string = 1 << 4,
    array = 1 << 5,
    object = 1 << 6,
    any = 0x7F,
};

constexpr type_hint operator|(type_hint a, type_hint b) noexcept {
    return static_cast<type_hint>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

constexpr bool has_any(type_hint set, type_hint flags) noexcept {
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(flags)) != 0;
}

template <typename T>
constexpr type_hint expected_type_hints() {
    using U = std::remove_cvref_t<T>;

    if constexpr(meta::annotated_type<U>) {
        return expected_type_hints<typename U::annotated_type>();
    } else if constexpr(kota::is_specialization_of<std::optional, U>) {
        return type_hint::null_like | expected_type_hints<typename U::value_type>();
    } else if constexpr(null_like<U>) {
        return type_hint::null_like;
    } else if constexpr(bool_like<U>) {
        return type_hint::boolean;
    } else if constexpr(int_like<U> || uint_like<U>) {
        return type_hint::integer;
    } else if constexpr(floating_like<U>) {
        // floats accept both integer and floating sources
        return type_hint::integer | type_hint::floating;
    } else if constexpr(char_like<U> || std::same_as<U, std::string> ||
                        std::derived_from<U, std::string>) {
        return type_hint::string;
    } else if constexpr(std::same_as<U, std::vector<std::byte>>) {
        return type_hint::array;
    } else if constexpr(is_pair_v<U> || is_tuple_v<U>) {
        return type_hint::array;
    } else if constexpr(std::ranges::input_range<U>) {
        constexpr auto kind = format_kind<U>;
        if constexpr(kind == range_format::map) {
            return type_hint::object;
        } else if constexpr(kind == range_format::sequence || kind == range_format::set) {
            return type_hint::array;
        } else {
            return type_hint::any;
        }
    } else if constexpr(meta::reflectable_class<U>) {
        return type_hint::object;
    } else {
        return type_hint::any;
    }
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

template <typename D, typename Source, typename... Ts>
auto try_variant_dispatch(Source&& source,
                          type_hint hint,
                          std::variant<Ts...>& value,
                          typename D::error_type mismatch_error)
    -> std::expected<void, typename D::error_type> {
    static_assert((std::default_initializable<Ts> && ...),
                  "variant deserialization requires default-constructible alternatives");

    using error_type = typename D::error_type;

    bool matched = false;
    bool considered = false;
    error_type last_error = mismatch_error;

    auto try_alternative = [&](auto type_tag) {
        if(matched) {
            return;
        }

        using alt_t = typename decltype(type_tag)::type;
        if(!has_any(expected_type_hints<alt_t>(), hint)) {
            return;
        }

        considered = true;
        auto status =
            try_deserialize_variant_candidate<D, alt_t>(std::forward<Source>(source), value);
        if(status) {
            matched = true;
        } else {
            last_error = status.error();
        }
    };

    (try_alternative(std::type_identity<Ts>{}), ...);

    if(!matched) {
        return std::unexpected(considered ? last_error : mismatch_error);
    }
    return {};
}

}  // namespace kota::codec
