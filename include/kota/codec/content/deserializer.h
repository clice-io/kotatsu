#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "kota/support/expected_try.h"
#include "kota/codec/deserialize.h"
#include "kota/codec/content/backend.h"
#include "kota/codec/content/document.h"
#include "kota/codec/content/error.h"

namespace kota::codec::content {

template <typename Config = config::default_config, typename T>
auto from_content(const Value& value, T& out) -> std::expected<void, error> {
    using Backend = content_backend_with_config<Config>;
    typename Backend::value_type src = &value;
    auto err = codec::deserialize<Backend>(src, out);
    if(err != error_kind::ok) {
        return std::unexpected(error(err));
    }
    return {};
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_content(const Value& value) -> std::expected<T, error> {
    T out{};
    KOTA_EXPECTED_TRY(from_content<Config>(value, out));
    return out;
}

}  // namespace kota::codec::content

namespace kota::codec {

/// custom_deserialize for content::Value: copy the value directly
template <>
struct custom_deserialize<content::content_backend, content::Value> {
    static auto read(const content::Value*& src, content::Value& out) -> content::error_kind {
        if(!src)
            return content::error_kind::type_mismatch;
        out = *src;
        return content::error_kind::ok;
    }
};

/// custom_deserialize for content::Array: extract array from value
template <>
struct custom_deserialize<content::content_backend, content::Array> {
    static auto read(const content::Value*& src, content::Array& out) -> content::error_kind {
        if(!src)
            return content::error_kind::type_mismatch;
        const content::Array* arr = src->get_array();
        if(!arr)
            return content::error_kind::type_mismatch;
        out = *arr;
        return content::error_kind::ok;
    }
};

/// custom_deserialize for content::Object: extract object from value
template <>
struct custom_deserialize<content::content_backend, content::Object> {
    static auto read(const content::Value*& src, content::Object& out) -> content::error_kind {
        if(!src)
            return content::error_kind::type_mismatch;
        const content::Object* obj = src->get_object();
        if(!obj)
            return content::error_kind::type_mismatch;
        out = *obj;
        return content::error_kind::ok;
    }
};

}  // namespace kota::codec
