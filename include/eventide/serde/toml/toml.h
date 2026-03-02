#pragma once

#include <concepts>
#include <expected>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "eventide/serde/toml/deserializer.h"
#include "eventide/serde/toml/error.h"
#include "eventide/serde/toml/serializer.h"

namespace eventide::serde::toml {

inline auto parse_table(std::string_view text) -> std::expected<::toml::table, error_kind> {
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
auto parse(std::string_view text, T& value) -> std::expected<void, error_kind> {
    auto table = parse_table(text);
    if(!table) {
        return std::unexpected(table.error());
    }
    return from_toml(*table, value);
}

template <typename T>
    requires std::default_initializable<T>
auto parse(std::string_view text) -> std::expected<T, error_kind> {
    auto table = parse_table(text);
    if(!table) {
        return std::unexpected(table.error());
    }
    return from_toml<T>(*table);
}

template <typename T>
auto to_string(const T& value) -> std::expected<std::string, error_kind> {
    auto table = to_toml(value);
    if(!table) {
        return std::unexpected(table.error());
    }

    std::ostringstream out;
    out << *table;
    return out.str();
}

}  // namespace eventide::serde::toml

namespace eventide::serde {

template <typename T>
concept toml_dynamic_dom_type = std::same_as<std::remove_cvref_t<T>, ::toml::table> ||
                                std::same_as<std::remove_cvref_t<T>, ::toml::array>;

template <toml_dynamic_dom_type T>
struct serialize_traits<toml::Serializer, T> {
    using value_type = typename toml::Serializer::value_type;
    using error_type = typename toml::Serializer::error_type;

    static auto serialize(toml::Serializer& serializer, const T& value)
        -> std::expected<value_type, error_type> {
        if constexpr(std::same_as<std::remove_cvref_t<T>, ::toml::table>) {
            return serializer.serialize_dom(value);
        } else {
            return serializer.serialize_dom(value);
        }
    }
};

template <>
struct deserialize_traits<toml::Deserializer, ::toml::table> {
    using error_type = toml::Deserializer::error_type;

    static auto deserialize(toml::Deserializer& deserializer, ::toml::table& value)
        -> std::expected<void, error_type> {
        auto table = deserializer.capture_table();
        if(!table) {
            return std::unexpected(table.error());
        }
        value = std::move(*table);
        return {};
    }
};

template <>
struct deserialize_traits<toml::Deserializer, ::toml::array> {
    using error_type = toml::Deserializer::error_type;

    static auto deserialize(toml::Deserializer& deserializer, ::toml::array& value)
        -> std::expected<void, error_type> {
        auto array = deserializer.capture_array();
        if(!array) {
            return std::unexpected(array.error());
        }
        value = std::move(*array);
        return {};
    }
};

}  // namespace eventide::serde
