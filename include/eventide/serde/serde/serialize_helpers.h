#pragma once

#include <string_view>

#include "eventide/serde/serde/serde.h"

namespace eventide::serde::detail {

/// Shared array serializer for streaming backends (JSON, flexbuffers, etc.).
/// `S` is the concrete Serializer type that provides `end_array()`.
template <typename S>
class SerializeArray {
public:
    explicit SerializeArray(S& serializer) noexcept : serializer(serializer) {}

    template <typename T>
    auto serialize_element(const T& value) -> typename S::status_t {
        auto result = serde::serialize(serializer, value);
        if(!result) {
            return std::unexpected(result.error());
        }
        return {};
    }

    auto end() -> typename S::template result_t<typename S::value_type> {
        return serializer.end_array();
    }

private:
    S& serializer;
};

/// Shared object serializer for streaming backends (JSON, flexbuffers, etc.).
/// `S` is the concrete Serializer type that provides `key()`, `end_object()`.
template <typename S>
class SerializeObject {
public:
    explicit SerializeObject(S& serializer) noexcept : serializer(serializer) {}

    template <typename K, typename V>
    auto serialize_entry(const K& key, const V& value) -> typename S::status_t {
        auto key_status = serializer.key(serde::spelling::map_key_to_string(key));
        if(!key_status) {
            return std::unexpected(key_status.error());
        }
        return serde::serialize(serializer, value);
    }

    template <typename T>
    auto serialize_field(std::string_view key, const T& value) -> typename S::status_t {
        auto key_status = serializer.key(key);
        if(!key_status) {
            return std::unexpected(key_status.error());
        }
        return serde::serialize(serializer, value);
    }

    auto end() -> typename S::template result_t<typename S::value_type> {
        return serializer.end_object();
    }

private:
    S& serializer;
};

}  // namespace eventide::serde::detail
