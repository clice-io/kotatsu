#pragma once

#include <cstddef>
#include <expected>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/support/expected_try.h"
#include "kota/support/type_list.h"
#include "kota/meta/schema.h"
#include "kota/meta/struct.h"
#include "kota/codec/detail/config.h"
#include "kota/codec/detail/struct_serialize.h"

namespace kota::codec::detail {

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

}  // namespace kota::codec::detail
