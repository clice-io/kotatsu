#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

#include "eventide/serde/bincode/bincode.h"
#include "eventide/serde/json/simd_deserializer.h"
#include "eventide/serde/json/simd_serializer.h"
#include "eventide/serde/serde.h"

namespace eventide::serde {

struct RawValue {
    std::string data;

    bool empty() const noexcept {
        return data.empty();
    }
};

// --- JSON serialization: inline raw JSON text ---

template <typename Config>
struct serialize_traits<json::simd::Serializer<Config>, RawValue> {
    using value_type = typename json::simd::Serializer<Config>::value_type;
    using error_type = typename json::simd::Serializer<Config>::error_type;

    static auto serialize(json::simd::Serializer<Config>& serializer, const RawValue& value)
        -> std::expected<value_type, error_type> {
        if(value.empty()) {
            return serializer.serialize_null();
        }
        return serializer.serialize_raw_json(value.data);
    }
};

// --- JSON deserialization: capture raw JSON view ---

template <typename Config>
struct deserialize_traits<json::simd::Deserializer<Config>, RawValue> {
    using error_type = typename json::simd::Deserializer<Config>::error_type;

    static auto deserialize(json::simd::Deserializer<Config>& deserializer, RawValue& value)
        -> std::expected<void, error_type> {
        auto raw = deserializer.deserialize_raw_json_view();
        if(!raw) {
            return std::unexpected(raw.error());
        }
        value.data.assign(raw->data(), raw->size());
        return {};
    }
};

// --- Bincode serialization: length-prefixed bytes ---

template <typename Config>
struct serialize_traits<bincode::Serializer<Config>, RawValue> {
    using value_type = typename bincode::Serializer<Config>::value_type;
    using error_type = typename bincode::Serializer<Config>::error_type;

    static auto serialize(bincode::Serializer<Config>& serializer, const RawValue& value)
        -> std::expected<value_type, error_type> {
        auto bytes =
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(value.data.data()),
                                       value.data.size());
        return serializer.serialize_bytes(bytes);
    }
};

// --- Bincode deserialization: length-prefixed bytes ---

template <typename Config>
struct deserialize_traits<bincode::Deserializer<Config>, RawValue> {
    using error_type = typename bincode::Deserializer<Config>::error_type;

    static auto deserialize(bincode::Deserializer<Config>& deserializer, RawValue& value)
        -> std::expected<void, error_type> {
        std::vector<std::byte> bytes;
        auto status = deserializer.deserialize_bytes(bytes);
        if(!status) {
            return std::unexpected(status.error());
        }
        value.data.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return {};
    }
};

}  // namespace eventide::serde
