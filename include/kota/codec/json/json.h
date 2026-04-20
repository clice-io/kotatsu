#pragma once

#include <concepts>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "kota/codec/config.h"
#include "kota/codec/content/deserializer.h"
#include "kota/codec/content/document.h"
#include "kota/codec/content/serializer.h"
#include "kota/codec/json/deserializer.h"
#include "kota/codec/json/error.h"
#include "kota/codec/json/serializer.h"
#include "kota/codec/raw_value.h"

namespace kota::codec::json {

// DOM type aliases (shared with content backend)
using ValueKind = content::ValueKind;
using Cursor = content::Cursor;
using Value = content::Value;
using Array = content::Array;
using Object = content::Object;

// Top-level convenience API (uses streaming simdjson backend by default)

template <typename Config = config::default_config, typename T>
auto parse(std::string_view json, T& value) -> std::expected<void, error> {
    return from_json<Config>(json, value);
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto parse(std::string_view json) -> std::expected<T, error> {
    return from_json<T, Config>(json);
}

template <typename Config = config::default_config, typename T>
auto to_string(const T& value, std::optional<std::size_t> initial_capacity = std::nullopt)
    -> std::expected<std::string, error> {
    return to_json<Config>(value, initial_capacity);
}

}  // namespace kota::codec::json

namespace kota::codec {

template <typename T>
concept json_dynamic_dom_type =
    std::same_as<T, json::Value> || std::same_as<T, json::Array> || std::same_as<T, json::Object>;

template <typename Config, json_dynamic_dom_type T>
struct deserialize_traits<json::Deserializer<Config>, T> {
    using error_type = json::error;

    static auto deserialize(json::Deserializer<Config>& deserializer, T& value)
        -> std::expected<void, error_type> {
        auto dom = deserializer.capture_dom_value();
        if(!dom) {
            return std::unexpected(dom.error());
        }
        if constexpr(std::same_as<T, json::Value>) {
            value = std::move(*dom);
            return {};
        } else if constexpr(std::same_as<T, json::Array>) {
            content::Array* arr = dom->get_array();
            if(arr == nullptr) {
                return std::unexpected(json::error_kind::type_mismatch);
            }
            value = std::move(*arr);
            return {};
        } else {
            content::Object* obj = dom->get_object();
            if(obj == nullptr) {
                return std::unexpected(json::error_kind::type_mismatch);
            }
            value = std::move(*obj);
            return {};
        }
    }
};

template <typename Config>
struct serialize_traits<json::Serializer<Config>, RawValue> {
    using value_type = typename json::Serializer<Config>::value_type;
    using error_type = typename json::Serializer<Config>::error_type;

    static auto serialize(json::Serializer<Config>& serializer, const RawValue& value)
        -> std::expected<value_type, error_type> {
        if(value.empty()) {
            return serializer.serialize_null();
        }
        return serializer.serialize_raw_json(value.data);
    }
};

template <typename Config>
struct deserialize_traits<json::Deserializer<Config>, RawValue> {
    using error_type = typename json::Deserializer<Config>::error_type;

    static auto deserialize(json::Deserializer<Config>& deserializer, RawValue& value)
        -> std::expected<void, error_type> {
        auto raw = deserializer.deserialize_raw_json_view();
        if(!raw) {
            return std::unexpected(raw.error());
        }
        value.data.assign(raw->data(), raw->size());
        return {};
    }
};

}  // namespace kota::codec
