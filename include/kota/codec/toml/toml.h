#pragma once

#include <concepts>
#include <expected>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "kota/codec/content/deserializer.h"
#include "kota/codec/content/document.h"
#include "kota/codec/toml/deserializer.h"
#include "kota/codec/toml/error.h"
#include "kota/codec/toml/serializer.h"

namespace kota::codec::toml {

inline auto parse_table(std::string_view text) -> std::expected<::toml::table, error> {
#if TOML_EXCEPTIONS
    try {
        return ::toml::parse(text);
    } catch(const ::toml::parse_error&) {
        return std::unexpected(error_kind::parse_error);
    }
#else
    auto parsed = ::toml::parse(text);
    if(!parsed) {
        return std::unexpected(error_kind::parse_error);
    }
    return std::move(parsed).table();
#endif
}

template <typename T>
auto parse(std::string_view text, T& value) -> std::expected<void, error> {
    auto table = parse_table(text);
    if(!table) {
        return std::unexpected(table.error());
    }
    return from_toml(*table, value);
}

template <typename T>
    requires std::default_initializable<T>
auto parse(std::string_view text) -> std::expected<T, error> {
    auto table = parse_table(text);
    if(!table) {
        return std::unexpected(table.error());
    }
    return from_toml<T>(*table);
}

template <typename T>
auto to_string(const T& value) -> std::expected<std::string, error> {
    auto table = to_toml(value);
    if(!table) {
        return std::unexpected(table.error());
    }

    std::ostringstream out;
    out << *table;
    return out.str();
}

}  // namespace kota::codec::toml

namespace kota::codec {

template <typename T>
concept toml_dynamic_dom_type = std::same_as<std::remove_cvref_t<T>, ::toml::table> ||
                                std::same_as<std::remove_cvref_t<T>, ::toml::array>;

template <typename Config, toml_dynamic_dom_type T>
struct serialize_traits<toml::Serializer<Config>, T> {
    using value_type = void;
    using error_type = typename toml::Serializer<Config>::error_type;

    static auto serialize(toml::Serializer<Config>& serializer, const T& value)
        -> std::expected<value_type, error_type> {
        return serializer.serialize_dom(value);
    }
};

/// deserialize_traits for ::toml::table: copy the table node directly
template <>
struct deserialize_traits<toml::toml_backend, ::toml::table> {
    static auto read(const ::toml::node*& src, ::toml::table& out) -> toml::error_kind {
        if(!src)
            return toml::error_kind::type_mismatch;
        const auto* tbl = src->as_table();
        if(!tbl)
            return toml::error_kind::type_mismatch;
        out = *tbl;
        return toml::error_kind::ok;
    }
};

/// deserialize_traits for ::toml::array: copy the array node directly
template <>
struct deserialize_traits<toml::toml_backend, ::toml::array> {
    static auto read(const ::toml::node*& src, ::toml::array& out) -> toml::error_kind {
        if(!src)
            return toml::error_kind::type_mismatch;
        const auto* arr = src->as_array();
        if(!arr)
            return toml::error_kind::type_mismatch;
        out = *arr;
        return toml::error_kind::ok;
    }
};

}  // namespace kota::codec
