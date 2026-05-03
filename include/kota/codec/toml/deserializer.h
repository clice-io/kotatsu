#pragma once

#include <concepts>
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
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/codec/deserialize.h"
#include "kota/codec/toml/backend.h"
#include "kota/codec/toml/error.h"
#include "kota/codec/toml/serializer.h"

namespace kota::codec::toml {

namespace detail {

using codec::detail::clean_t;
using codec::detail::remove_annotation_t;
using codec::detail::remove_optional_t;

template <typename T>
consteval bool is_map_like() {
    if constexpr(std::ranges::input_range<T>) {
        return format_kind<T> == range_format::map;
    } else {
        return false;
    }
}

template <typename T>
constexpr bool is_map_like_v = is_map_like<T>();

template <typename T>
constexpr bool root_table_v = (meta::reflectable_class<T> && !is_pair_v<T> && !is_tuple_v<T> &&
                               !std::ranges::input_range<T>) ||
                              is_map_like_v<T> || std::same_as<T, ::toml::table>;

template <typename T>
auto select_root_node(const ::toml::table& table) -> const ::toml::node* {
    using U = std::remove_cvref_t<T>;

    if constexpr(is_specialization_of<std::optional, U>) {
        if(table.empty()) {
            return nullptr;
        }
        using value_t = clean_t<U>;
        if constexpr(root_table_v<value_t>) {
            return std::addressof(static_cast<const ::toml::node&>(table));
        } else {
            return table.get(boxed_root_key);
        }
    } else if constexpr(root_table_v<clean_t<U>>) {
        return std::addressof(static_cast<const ::toml::node&>(table));
    } else {
        return table.get(boxed_root_key);
    }
}

}  // namespace detail

template <typename Config = config::default_config, typename T>
auto from_toml(const ::toml::table& table, T& value) -> std::expected<void, error> {
    using Backend = toml_backend_with_config<Config>;
    detail_v2::thread_error_context().clear();

    typename Backend::value_type root = detail::select_root_node<T>(table);
    auto err = codec::deserialize<Backend>(root, value);
    if(err != error_kind::ok) {
        auto& ctx = detail_v2::thread_error_context();
        auto pending = ctx.take();
        if(pending) {
            return std::unexpected(std::move(*pending));
        }
        return std::unexpected(error(err));
    }
    return {};
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_toml(const ::toml::table& table) -> std::expected<T, error> {
    T value{};
    KOTA_EXPECTED_TRY(from_toml<Config>(table, value));
    return value;
}

}  // namespace kota::codec::toml
