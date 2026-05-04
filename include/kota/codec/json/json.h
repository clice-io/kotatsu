#pragma once

#include <concepts>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "kota/codec/content/deserializer.h"
#include "kota/codec/content/document.h"
#include "kota/codec/content/serializer.h"
#include "kota/codec/detail/config.h"
#include "kota/codec/detail/raw_value.h"
#include "kota/codec/json/deserializer.h"
#include "kota/codec/json/error.h"
#include "kota/codec/json/serializer.h"

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

inline std::expected<std::string, error> prettify(std::string_view json) {
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    auto padded = simdjson::padded_string(json);
    if(auto err = parser.parse(padded).get(doc)) {
        return std::unexpected(error(make_error(err)));
    }
    return simdjson::prettify(doc);
}

}  // namespace kota::codec::json

namespace kota::codec {

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

/// deserialize_traits for RawValue: captures the raw JSON text of a value
template <>
struct deserialize_traits<json::simdjson_backend, RawValue> {
    static auto read(json::simdjson_backend::value_type& src, RawValue& out)
        -> json::simdjson_backend::error_type {
        auto [raw, err] = json::simdjson_backend::capture_raw_json(src);
        if(err != simdjson::SUCCESS)
            return err;
        out.data = std::move(raw);
        return simdjson::SUCCESS;
    }
};

}  // namespace kota::codec
