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

/// custom_deserialize for RawValue: captures the raw JSON text of a value
template <>
struct custom_deserialize<json::simdjson_backend, RawValue> {
    static auto read(json::simdjson_backend::value_type& val, RawValue& out)
        -> json::simdjson_backend::error_type {
        std::string_view raw;
        auto err = val.raw_json().get(raw);
        if(err != simdjson::SUCCESS)
            return err;
        out.data.assign(raw.data(), raw.size());
        return simdjson::SUCCESS;
    }
};

/// custom_deserialize for content::Value: type-aware recursive deserialization
template <>
struct custom_deserialize<json::simdjson_backend, content::Value> {
    using Backend = json::simdjson_backend;

    static auto read(Backend::value_type& val, content::Value& out) -> Backend::error_type {
        auto kind = Backend::kind_of(val);

        if(kind == meta::type_kind::null) {
            bool is_null = false;
            auto err = Backend::read_is_null(val, is_null);
            if(err != Backend::success)
                return err;
            out = content::Value(nullptr);
            return Backend::success;
        }
        if(kind == meta::type_kind::boolean) {
            bool b = false;
            auto err = Backend::read_bool(val, b);
            if(err != Backend::success)
                return err;
            out = content::Value(b);
            return Backend::success;
        }
        if(kind == meta::type_kind::uint64) {
            std::uint64_t u = 0;
            auto err = Backend::read_uint64(val, u);
            if(err != Backend::success)
                return err;
            out = content::Value(u);
            return Backend::success;
        }
        if(meta::is_integer_kind(kind)) {
            std::int64_t i = 0;
            auto err = Backend::read_int64(val, i);
            if(err != Backend::success)
                return err;
            out = content::Value(i);
            return Backend::success;
        }
        if(meta::is_floating_kind(kind)) {
            double d = 0.0;
            auto err = Backend::read_double(val, d);
            if(err != Backend::success)
                return err;
            out = content::Value(d);
            return Backend::success;
        }
        if(kind == meta::type_kind::string) {
            std::string_view sv;
            auto err = Backend::read_string(val, sv);
            if(err != Backend::success)
                return err;
            out = content::Value(std::string(sv));
            return Backend::success;
        }
        if(meta::is_sequence_kind(kind)) {
            content::Array arr;
            struct array_visitor {
                content::Array& arr;
                Backend::error_type visit_element(Backend::value_type& elem) {
                    content::Value v;
                    auto err = custom_deserialize::read(elem, v);
                    if(err != Backend::success)
                        return err;
                    arr.push_back(std::move(v));
                    return Backend::success;
                }
            };
            array_visitor vis{arr};
            auto err = Backend::visit_array(val, vis);
            if(err != Backend::success)
                return err;
            out = content::Value(std::move(arr));
            return Backend::success;
        }
        if(meta::is_object_kind(kind)) {
            content::Object obj;
            struct object_visitor {
                content::Object& obj;
                Backend::error_type visit_field(std::string_view key, Backend::value_type& field_val) {
                    content::Value v;
                    auto err = custom_deserialize::read(field_val, v);
                    if(err != Backend::success)
                        return err;
                    obj.insert(std::string(key), std::move(v));
                    return Backend::success;
                }
            };
            object_visitor vis{obj};
            auto err = Backend::visit_object(val, vis);
            if(err != Backend::success)
                return err;
            out = content::Value(std::move(obj));
            return Backend::success;
        }
        return Backend::type_mismatch;
    }
};

}  // namespace kota::codec
