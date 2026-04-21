#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/support/ranges.h"
#include "kota/support/type_list.h"
#include "kota/support/type_traits.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/codec/config.h"
#include "kota/codec/detail/arena_traits.h"
#include "kota/codec/detail/common.h"
#include "kota/codec/detail/dispatch.h"
#include "kota/codec/traits.h"

namespace kota::codec::arena {

namespace detail {

using codec::detail::clean_t;

template <typename B, typename T>
concept backend_can_inline_struct_field = B::template can_inline_struct_field<T>;

template <typename B, typename T>
concept backend_can_inline_struct_element = B::template can_inline_struct_element<T>;

template <typename Config, typename B, typename Raw, typename Attrs, typename V>
auto encode_value_at(B& b, typename B::TableBuilder& tb, typename B::slot_id sid, const V& value)
    -> std::expected<void, typename B::error_type>;

template <typename Config, typename B, typename T>
auto encode_table(B& b, const T& value)
    -> std::expected<typename B::table_ref, typename B::error_type>;

template <typename Config, typename B, typename T>
auto encode_tuple_like(B& b, const T& value)
    -> std::expected<typename B::table_ref, typename B::error_type>;

template <typename Config, typename B, typename T>
auto encode_variant(B& b, const T& value)
    -> std::expected<typename B::table_ref, typename B::error_type>;

template <typename Config, typename B, typename T>
auto encode_sequence(B& b, const T& range)
    -> std::expected<typename B::vector_ref, typename B::error_type>;

template <typename Config, typename B, typename T>
auto encode_map(B& b, const T& map)
    -> std::expected<typename B::vector_ref, typename B::error_type>;

template <typename Config, typename B, typename T>
auto encode_boxed(B& b, const T& value)
    -> std::expected<typename B::table_ref, typename B::error_type>;

template <typename Config, typename B, typename T>
auto encode_root(B& b, const T& value)
    -> std::expected<typename B::table_ref, typename B::error_type> {
    using U = std::remove_cvref_t<T>;

    if constexpr(meta::annotated_type<U>) {
        return encode_root<Config>(b, meta::annotated_value(value));
    } else if constexpr(is_specialization_of<std::optional, U>) {
        if(!value.has_value()) {
            return encode_boxed<Config>(b, value);
        }
        return encode_root<Config>(b, *value);
    } else if constexpr(meta::reflectable_class<U> && !B::template can_inline_struct_field<U> &&
                        !std::ranges::input_range<U> && !is_pair_v<U> && !is_tuple_v<U>) {
        return encode_table<Config>(b, value);
    } else if constexpr(is_specialization_of<std::variant, U>) {
        return encode_variant<Config>(b, value);
    } else if constexpr(is_pair_v<U> || is_tuple_v<U>) {
        return encode_tuple_like<Config>(b, value);
    } else {
        return encode_boxed<Config>(b, value);
    }
}

template <typename Config, typename B, typename T>
auto encode_boxed(B& b, const T& value)
    -> std::expected<typename B::table_ref, typename B::error_type> {
    auto tb = b.start_table();
    KOTA_EXPECTED_TRY_V(auto sid, B::field_slot_id(0));
    KOTA_EXPECTED_TRY(
        (encode_value_at<Config, B, std::remove_cvref_t<T>, std::tuple<>>(b, tb, sid, value)));
    return tb.finalize();
}

template <typename Config, typename B, typename T, std::size_t I>
auto encode_struct_slot(B& b, typename B::TableBuilder& tb, const T& value)
    -> std::expected<void, typename B::error_type> {
    using schema = meta::virtual_schema<T, Config>;
    using slot_t = kota::type_list_element_t<I, typename schema::slots>;
    using raw_t = std::remove_cv_t<typename slot_t::raw_type>;
    using attrs_t = typename slot_t::attrs;

    constexpr std::size_t offset = schema::fields[I].offset;
    const auto* base = reinterpret_cast<const std::byte*>(std::addressof(value));
    const auto& field_value = *reinterpret_cast<const raw_t*>(base + offset);

    if constexpr(kota::tuple_has_spec_v<attrs_t, meta::behavior::skip_if>) {
        using pred = typename kota::tuple_find_spec_t<attrs_t, meta::behavior::skip_if>::predicate;
        if(meta::evaluate_skip_predicate<pred>(field_value, /*is_serialize=*/true)) {
            return {};
        }
    }

    KOTA_EXPECTED_TRY_V(auto sid, B::field_slot_id(I));
    return encode_value_at<Config, B, raw_t, attrs_t>(b, tb, sid, field_value);
}

template <typename Config, typename B, typename T>
auto encode_table(B& b, const T& value)
    -> std::expected<typename B::table_ref, typename B::error_type> {
    using E = typename B::error_type;
    using schema = meta::virtual_schema<T, Config>;
    using slots = typename schema::slots;
    constexpr std::size_t N = kota::type_list_size_v<slots>;

    auto tb = b.start_table();

    std::expected<void, E> status{};
    bool ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return ([&] {
            auto r = encode_struct_slot<Config, B, T, Is>(b, tb, value);
            if(!r) {
                status = std::unexpected(r.error());
                return false;
            }
            return true;
        }() && ...);
    }(std::make_index_sequence<N>{});

    if(!ok) {
        return std::unexpected(status.error());
    }
    return tb.finalize();
}

template <typename Config, typename B, typename T>
auto encode_tuple_like(B& b, const T& value)
    -> std::expected<typename B::table_ref, typename B::error_type> {
    using E = typename B::error_type;
    using U = std::remove_cvref_t<T>;

    auto tb = b.start_table();

    std::expected<void, E> status{};
    bool ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return ([&] {
            auto sid_r = B::field_slot_id(Is);
            if(!sid_r) {
                status = std::unexpected(sid_r.error());
                return false;
            }
            using element_t = std::tuple_element_t<Is, U>;
            auto r = encode_value_at<Config, B, element_t, std::tuple<>>(b,
                                                                         tb,
                                                                         *sid_r,
                                                                         std::get<Is>(value));
            if(!r) {
                status = std::unexpected(r.error());
                return false;
            }
            return true;
        }() && ...);
    }(std::make_index_sequence<std::tuple_size_v<U>>{});

    if(!ok) {
        return std::unexpected(status.error());
    }
    return tb.finalize();
}

template <typename Config, typename B, typename T>
auto encode_variant(B& b, const T& value)
    -> std::expected<typename B::table_ref, typename B::error_type> {
    using E = typename B::error_type;
    using U = std::remove_cvref_t<T>;
    static_assert(is_specialization_of<std::variant, U>, "variant required");

    auto tb = b.start_table();
    tb.add_scalar(B::variant_tag_slot_id(), static_cast<std::uint32_t>(value.index()));

    std::expected<void, E> status{};
    bool matched = false;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (([&] {
             if(value.index() != Is) {
                 return;
             }
             matched = true;
             auto sid_r = B::variant_payload_slot_id(Is);
             if(!sid_r) {
                 status = std::unexpected(sid_r.error());
                 return;
             }
             using alt_t = std::variant_alternative_t<Is, U>;
             auto r = encode_value_at<Config, B, alt_t, std::tuple<>>(b,
                                                                      tb,
                                                                      *sid_r,
                                                                      std::get<Is>(value));
             if(!r) {
                 status = std::unexpected(r.error());
             }
         }()),
         ...);
    }(std::make_index_sequence<std::variant_size_v<U>>{});

    if(!matched) {
        return std::unexpected(E::invalid_state);
    }
    if(!status) {
        return std::unexpected(status.error());
    }
    return tb.finalize();
}

template <typename Config, typename B, typename Raw, typename Attrs, typename V>
auto encode_value_at(B& b, typename B::TableBuilder& tb, typename B::slot_id sid, const V& value)
    -> std::expected<void, typename B::error_type> {
    using U = std::remove_cvref_t<V>;

    if constexpr(arena::streaming_serialize_traits<B, U>) {
        using traits = kota::codec::serialize_traits<B, std::remove_cvref_t<U>>;
        KOTA_EXPECTED_TRY_V(auto r, traits::serialize(b, value));
        tb.add_offset(sid, r);
        return {};
    } else if constexpr(arena::value_serialize_traits<B, U>) {
        using traits = kota::codec::serialize_traits<B, std::remove_cvref_t<U>>;
        using wire_t = typename traits::wire_type;
        wire_t wire = traits::serialize(b, value);
        return encode_value_at<Config, B, wire_t, std::tuple<>>(b, tb, sid, wire);
    } else {
        codec::detail::ArenaFieldCtx<B> ctx{b, tb, sid};
        return codec::detail::unified_serialize<Config, codec::detail::ArenaFieldCtx<B>, Attrs>(
            ctx,
            value);
    }
}

template <typename Config, typename B, typename T>
auto encode_sequence(B& b, const T& range)
    -> std::expected<typename B::vector_ref, typename B::error_type> {
    using U = std::remove_cvref_t<T>;
    using element_t = std::ranges::range_value_t<U>;
    using element_clean_t = detail::clean_t<element_t>;

    if constexpr(arena::value_serialize_traits<B, element_clean_t>) {
        using traits = kota::codec::serialize_traits<B, element_clean_t>;
        using wire_t = typename traits::wire_type;
        std::vector<wire_t> transformed;
        if constexpr(requires { range.size(); }) {
            transformed.reserve(range.size());
        }
        for(const auto& e: range) {
            transformed.push_back(traits::serialize(b, e));
        }
        return encode_sequence<Config>(b, transformed);
    } else if constexpr(std::same_as<element_clean_t, std::byte>) {
        std::vector<std::byte> bytes;
        if constexpr(requires { range.size(); }) {
            bytes.reserve(range.size());
        }
        for(auto bt: range) {
            bytes.push_back(bt);
        }
        return b.alloc_bytes(std::span<const std::byte>(bytes.data(), bytes.size()));
    } else if constexpr(codec::bool_like<element_clean_t> || codec::int_like<element_clean_t> ||
                        codec::uint_like<element_clean_t>) {
        std::vector<element_clean_t> elements;
        if constexpr(requires { range.size(); }) {
            elements.reserve(range.size());
        }
        for(const auto& e: range) {
            elements.push_back(static_cast<element_clean_t>(e));
        }
        return b.template alloc_scalar_vector<element_clean_t>(
            std::span<const element_clean_t>(elements.data(), elements.size()));
    } else if constexpr(codec::floating_like<element_clean_t>) {
        if constexpr(std::same_as<element_clean_t, float> ||
                     std::same_as<element_clean_t, double>) {
            std::vector<element_clean_t> elements;
            if constexpr(requires { range.size(); }) {
                elements.reserve(range.size());
            }
            for(const auto& e: range) {
                elements.push_back(static_cast<element_clean_t>(e));
            }
            return b.template alloc_scalar_vector<element_clean_t>(
                std::span<const element_clean_t>(elements.data(), elements.size()));
        } else {
            std::vector<double> elements;
            if constexpr(requires { range.size(); }) {
                elements.reserve(range.size());
            }
            for(const auto& e: range) {
                elements.push_back(static_cast<double>(e));
            }
            return b.template alloc_scalar_vector<double>(
                std::span<const double>(elements.data(), elements.size()));
        }
    } else if constexpr(codec::char_like<element_clean_t>) {
        std::vector<std::int8_t> elements;
        if constexpr(requires { range.size(); }) {
            elements.reserve(range.size());
        }
        for(const auto& e: range) {
            elements.push_back(static_cast<std::int8_t>(e));
        }
        return b.template alloc_scalar_vector<std::int8_t>(
            std::span<const std::int8_t>(elements.data(), elements.size()));
    } else if constexpr(codec::str_like<element_clean_t>) {
        std::vector<typename B::string_ref> elements;
        if constexpr(requires { range.size(); }) {
            elements.reserve(range.size());
        }
        for(const auto& e: range) {
            const std::string_view text = e;
            KOTA_EXPECTED_TRY_V(auto r, b.alloc_string(text));
            elements.push_back(r);
        }
        return b.alloc_string_vector(
            std::span<const typename B::string_ref>(elements.data(), elements.size()));
    } else if constexpr(is_pair_v<element_clean_t> || is_tuple_v<element_clean_t>) {
        std::vector<typename B::table_ref> elements;
        if constexpr(requires { range.size(); }) {
            elements.reserve(range.size());
        }
        for(const auto& e: range) {
            KOTA_EXPECTED_TRY_V(auto r, encode_tuple_like<Config>(b, e));
            elements.push_back(r);
        }
        return b.alloc_table_vector(
            std::span<const typename B::table_ref>(elements.data(), elements.size()));
    } else if constexpr(B::template can_inline_struct_element<element_clean_t>) {
        std::vector<element_clean_t> elements;
        if constexpr(requires { range.size(); }) {
            elements.reserve(range.size());
        }
        for(const auto& e: range) {
            elements.push_back(static_cast<element_clean_t>(e));
        }
        return b.template alloc_inline_struct_vector<element_clean_t>(
            std::span<const element_clean_t>(elements.data(), elements.size()));
    } else if constexpr(meta::reflectable_class<element_clean_t>) {
        std::vector<typename B::table_ref> elements;
        if constexpr(requires { range.size(); }) {
            elements.reserve(range.size());
        }
        for(const auto& e: range) {
            KOTA_EXPECTED_TRY_V(auto r, encode_table<Config>(b, e));
            elements.push_back(r);
        }
        return b.alloc_table_vector(
            std::span<const typename B::table_ref>(elements.data(), elements.size()));
    } else {
        std::vector<typename B::table_ref> elements;
        if constexpr(requires { range.size(); }) {
            elements.reserve(range.size());
        }
        for(const auto& e: range) {
            KOTA_EXPECTED_TRY_V(auto r, encode_boxed<Config>(b, e));
            elements.push_back(r);
        }
        return b.alloc_table_vector(
            std::span<const typename B::table_ref>(elements.data(), elements.size()));
    }
}

template <typename Config, typename B, typename T>
auto encode_map(B& b, const T& map)
    -> std::expected<typename B::vector_ref, typename B::error_type> {
    using U = std::remove_cvref_t<T>;
    using ref_t = std::remove_cvref_t<std::ranges::range_reference_t<U>>;
    using key_t = map_entry_key_t<ref_t>;
    using mapped_t = map_entry_mapped_t<ref_t>;

    std::vector<std::pair<key_t, mapped_t>> entries;
    entries.reserve(map.size());
    for(const auto& [k, v]: map) {
        entries.emplace_back(k, v);
    }
    if constexpr(requires(const key_t& a, const key_t& b) {
                     { a < b } -> std::convertible_to<bool>;
                 }) {
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
    }

    std::vector<typename B::table_ref> offsets;
    offsets.reserve(entries.size());
    for(const auto& [k, v]: entries) {
        auto tb = b.start_table();
        KOTA_EXPECTED_TRY_V(auto key_sid, B::field_slot_id(0));
        KOTA_EXPECTED_TRY((encode_value_at<Config, B, key_t, std::tuple<>>(b, tb, key_sid, k)));
        KOTA_EXPECTED_TRY_V(auto val_sid, B::field_slot_id(1));
        KOTA_EXPECTED_TRY((encode_value_at<Config, B, mapped_t, std::tuple<>>(b, tb, val_sid, v)));
        KOTA_EXPECTED_TRY_V(auto entry_ref, tb.finalize());
        offsets.push_back(entry_ref);
    }
    return b.alloc_table_vector(
        std::span<const typename B::table_ref>(offsets.data(), offsets.size()));
}

}  // namespace detail

using detail::encode_boxed;
using detail::encode_map;
using detail::encode_root;
using detail::encode_sequence;
using detail::encode_table;
using detail::encode_tuple_like;
using detail::encode_value_at;
using detail::encode_variant;

}  // namespace kota::codec::arena
