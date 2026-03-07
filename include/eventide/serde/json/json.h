#pragma once

#include <concepts>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "eventide/serde/content/deserializer.h"
#include "eventide/serde/content/dom.h"
#include "eventide/serde/content/serializer.h"
#include "eventide/serde/json/deserializer.h"
#include "eventide/serde/json/error.h"
#include "eventide/serde/json/serializer.h"
#include "eventide/serde/serde/config.h"

namespace eventide::serde::json {

// DOM type aliases (previously in json/dom.h)
using ValueKind = content::ValueKind;
using TaggedRef = content::TaggedRef;
using ValueRef = content::ValueRef;
using ArrayRef = content::ArrayRef;
using ObjectRef = content::ObjectRef;
using OwnedDoc = content::OwnedDoc;
using Value = content::Value;
using Array = content::Array;
using Object = content::Object;
using Document = content::Document;

template <typename T>
constexpr inline bool dom_writable_char_array_v = content::dom_writable_char_array_v<T>;

template <typename T>
constexpr inline bool dom_writable_value_v = content::dom_writable_value_v<T>;

template <typename T>
concept dom_writable_value = content::dom_writable_value<T>;

// yy (DOM-based) backend aliases
namespace yy {

template <typename Config = config::default_config>
using Serializer = content::Serializer<Config>;

template <typename Config = config::default_config>
using Deserializer = content::Deserializer<Config>;

template <typename Config = config::default_config, typename T>
auto to_json(const T& value)
    -> std::expected<std::string, typename Serializer<Config>::error_type> {
    Serializer<Config> serializer;
    if(!serializer.valid()) {
        return std::unexpected(serializer.error());
    }

    auto result = serde::serialize(serializer, value);
    if(!result) {
        return std::unexpected(result.error());
    }

    auto json = serializer.str();
    if(!json) {
        return std::unexpected(json.error());
    }
    return std::move(*json);
}

}  // namespace yy

// Top-level convenience API (uses streaming simdjson backend by default)

template <typename T>
auto parse(std::string_view json, T& value) -> std::expected<void, error_kind> {
    return from_json(json, value);
}

template <typename T>
    requires std::default_initializable<T>
auto parse(std::string_view json) -> std::expected<T, error_kind> {
    return from_json<T>(json);
}

template <typename T>
auto to_string(const T& value, std::optional<std::size_t> initial_capacity = std::nullopt)
    -> std::expected<std::string, error_kind> {
    return to_json(value, initial_capacity);
}

}  // namespace eventide::serde::json

namespace eventide::serde {

template <typename T>
concept json_dynamic_dom_type =
    std::same_as<T, json::Value> || std::same_as<T, json::Array> || std::same_as<T, json::Object>;

template <typename Config, json_dynamic_dom_type T>
struct deserialize_traits<json::Deserializer<Config>, T> {
    using error_type = json::error_kind;

    static auto deserialize(json::Deserializer<Config>& deserializer, T& value)
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

template <typename Config, json_dynamic_dom_type T>
struct serialize_traits<json::Serializer<Config>, T> {
    using value_type = typename json::Serializer<Config>::value_type;
    using error_type = typename json::Serializer<Config>::error_type;

    static auto serialize(json::Serializer<Config>& serializer, const T& value)
        -> std::expected<value_type, error_type> {
        auto raw = value.to_json_string();
        if(!raw) {
            return std::unexpected(json::make_write_error(raw.error()));
        }
        return serializer.serialize_raw_json(*raw);
    }
};

template <typename Config, json_dynamic_dom_type T>
struct serialize_traits<json::yy::Serializer<Config>, T> {
    using value_type = typename json::yy::Serializer<Config>::value_type;
    using error_type = typename json::yy::Serializer<Config>::error_type;

    static auto serialize(json::yy::Serializer<Config>& serializer, const T& value)
        -> std::expected<value_type, error_type> {
        return serializer.append_dom_value(value);
    }
};

template <typename Config, json_dynamic_dom_type T>
struct deserialize_traits<json::yy::Deserializer<Config>, T> {
    using error_type = typename json::yy::Deserializer<Config>::error_type;

    static auto deserialize(json::yy::Deserializer<Config>& deserializer, T& value)
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
