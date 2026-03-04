#pragma once

#include <concepts>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "eventide/serde/json/dom.h"
#include "eventide/serde/json/error.h"
#include "eventide/serde/json/simd_deserializer.h"
#include "eventide/serde/json/simd_serializer.h"
#include "eventide/serde/json/yy_deserializer.h"
#include "eventide/serde/json/yy_serializer.h"

namespace eventide::serde::json {

template <typename T>
auto parse(std::string_view json, T& value) -> std::expected<void, error_kind> {
    return simd::from_json(json, value);
}

template <typename T>
    requires std::default_initializable<T>
auto parse(std::string_view json) -> std::expected<T, error_kind> {
    return simd::from_json<T>(json);
}

template <typename T>
auto to_string(const T& value, std::optional<std::size_t> initial_capacity = std::nullopt)
    -> std::expected<std::string, error_kind> {
    return simd::to_json(value, initial_capacity);
}

}  // namespace eventide::serde::json

namespace eventide::serde {

template <typename T>
concept json_dynamic_dom_type =
    std::same_as<T, json::Value> || std::same_as<T, json::Array> || std::same_as<T, json::Object>;

template <json_dynamic_dom_type T>
struct deserialize_traits<json::simd::Deserializer, T> {
    using error_type = json::error_kind;

    static auto deserialize(json::simd::Deserializer& deserializer, T& value)
        -> std::expected<void, error_type> {
        auto raw = deserializer.deserialize_raw_json_view();
        if(!raw) {
            return std::unexpected(raw.error());
        }

        auto parsed = T::parse(std::string_view(*raw));
        if(!parsed) {
            return std::unexpected(json::make_read_error(parsed.error()));
        }

        value = std::move(*parsed);
        return {};
    }
};

template <json_dynamic_dom_type T>
struct serialize_traits<json::simd::Serializer, T> {
    using value_type = typename json::simd::Serializer::value_type;
    using error_type = typename json::simd::Serializer::error_type;

    static auto serialize(json::simd::Serializer& serializer, const T& value)
        -> std::expected<value_type, error_type> {
        auto raw = value.to_json_string();
        if(!raw) {
            return std::unexpected(json::make_write_error(raw.error()));
        }
        return serializer.serialize_raw_json(*raw);
    }
};

template <json_dynamic_dom_type T>
struct serialize_traits<json::yy::Serializer, T> {
    using value_type = typename json::yy::Serializer::value_type;
    using error_type = typename json::yy::Serializer::error_type;

    static auto serialize(json::yy::Serializer& serializer, const T& value)
        -> std::expected<value_type, error_type> {
        return serializer.append_json_value(value);
    }
};

template <json_dynamic_dom_type T>
struct deserialize_traits<json::yy::Deserializer, T> {
    using error_type = typename json::yy::Deserializer::error_type;

    static auto deserialize(json::yy::Deserializer& deserializer, T& value)
        -> std::expected<void, error_type> {
        auto dom = deserializer.capture_dom_value();
        if(!dom) {
            return std::unexpected(dom.error());
        } else if constexpr(std::same_as<T, json::Value>) {
            value = std::move(*dom);
            return {};
        } else if constexpr(std::same_as<T, json::Array>) {
            auto array = dom->get_array();
            if(!array) {
                return std::unexpected(json::error_kind::type_mismatch);
            }
            value = std::move(*array);
            return {};
        } else {
            auto object = dom->get_object();
            if(!object) {
                return std::unexpected(json::error_kind::type_mismatch);
            }
            value = std::move(*object);
            return {};
        }
    }
};

}  // namespace eventide::serde
