#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/codec/serde.h"

namespace kota::codec::detail {

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

/// Generic index-based array deserializer for DOM-like backends.
///
/// Requirements on D (parent Deserializer):
///   - D::error_type, D::result_t<T>, D::status_t
///   - d.valid() -> bool
///   - d.error() -> error_type
///   - d.mark_invalid() -> std::unexpected<error_type>
///   - d.deserialize_element_value(ElementRef, T&) -> status_t
///
/// ElementRef: the type used to reference an individual array element.
/// ArrayLike: the container holding elements, must support operator[] and size().
template <typename D, typename ArrayLike>
class IndexedArrayDeserializer {
public:
    using error_type = typename D::error_type;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    result_t<bool> has_next() {
        if(!deserializer.valid()) {
            return std::unexpected(deserializer.error());
        }
        return index < array_size;
    }

    template <typename T>
    status_t deserialize_element(T& value) {
        KOTA_EXPECTED_TRY_V(auto has_next_result, has_next());
        if(!has_next_result) {
            return deserializer.mark_invalid();
        }

        KOTA_EXPECTED_TRY(deserializer.deserialize_element_value(array, index, value));

        ++index;
        ++consumed_count;
        return {};
    }

    status_t skip_element() {
        KOTA_EXPECTED_TRY_V(auto has_next_result, has_next());
        if(!has_next_result) {
            return deserializer.mark_invalid();
        }

        ++index;
        ++consumed_count;
        return {};
    }

    status_t end() {
        if(!deserializer.valid()) {
            return std::unexpected(deserializer.error());
        }

        KOTA_EXPECTED_TRY_V(auto has_next_result, has_next());

        if(strict_length) {
            if(consumed_count != expected_length || has_next_result) {
                return deserializer.mark_invalid();
            }
            return {};
        }

        index = array_size;
        return {};
    }

protected:
    template <typename>
    friend class IndexedArrayAccess;

    IndexedArrayDeserializer(D& deserializer,
                             ArrayLike array,
                             std::size_t array_size,
                             std::size_t expected_length,
                             bool strict_length) :
        deserializer(deserializer), array(array), array_size(array_size),
        expected_length(expected_length), strict_length(strict_length) {}

    D& deserializer;
    ArrayLike array;
    std::size_t array_size = 0;
    std::size_t index = 0;
    std::size_t expected_length = 0;
    std::size_t consumed_count = 0;
    bool strict_length = false;
};

/// Generic indexed object deserializer for DOM-like backends.
///
/// Requirements on D (parent Deserializer):
///   - D::error_type, D::result_t<T>, D::status_t
///   - d.valid() -> bool
///   - d.error() -> error_type
///   - d.mark_invalid() -> std::unexpected<error_type>
///   - d.deserialize_entry_value(ValueType, T&) -> status_t
///
/// ValueType: the type used to reference an object value.
template <typename D, typename ValueType>
class IndexedObjectDeserializer {
public:
    using error_type = typename D::error_type;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    struct entry {
        std::string_view key;
        ValueType value;
    };

    result_t<std::optional<std::string_view>> next_key() {
        if(!deserializer.valid()) {
            return std::unexpected(deserializer.error());
        }
        if(has_pending_value) {
            return deserializer.mark_invalid();
        }
        if(index == entries.size()) {
            return std::optional<std::string_view>{};
        }

        has_pending_value = true;
        return std::optional<std::string_view>{entries[index].key};
    }

    status_t invalid_key(std::string_view /*key_name*/) {
        if(!deserializer.valid()) {
            return std::unexpected(deserializer.error());
        }
        if(!has_pending_value) {
            return deserializer.mark_invalid();
        }

        ++index;
        has_pending_value = false;
        return {};
    }

    template <typename T>
    status_t deserialize_value(T& value) {
        if(!deserializer.valid()) {
            return std::unexpected(deserializer.error());
        }
        if(!has_pending_value) {
            return deserializer.mark_invalid();
        }

        KOTA_EXPECTED_TRY(deserializer.deserialize_entry_value(entries[index].value, value));

        ++index;
        has_pending_value = false;
        return {};
    }

    status_t skip_value() {
        if(!deserializer.valid()) {
            return std::unexpected(deserializer.error());
        }
        if(!has_pending_value) {
            return deserializer.mark_invalid();
        }

        ++index;
        has_pending_value = false;
        return {};
    }

    status_t end() {
        if(!deserializer.valid()) {
            return std::unexpected(deserializer.error());
        }

        if(has_pending_value) {
            KOTA_EXPECTED_TRY(skip_value());
        }

        index = entries.size();
        return {};
    }

protected:
    template <typename>
    friend class IndexedObjectAccess;

    explicit IndexedObjectDeserializer(D& deserializer) : deserializer(deserializer) {}

    IndexedObjectDeserializer(D& deserializer, std::vector<entry> entries) :
        deserializer(deserializer), entries(std::move(entries)) {}

    D& deserializer;
    std::vector<entry> entries;
    std::size_t index = 0;
    bool has_pending_value = false;
};

/// Shared implementation: deserialize bytes as a sequence of uint8 values.
/// Used by JSON (simd, yy) and TOML backends that represent bytes as arrays.
template <deserializer_like D>
auto deserialize_bytes_from_seq(D& d, std::vector<std::byte>& value)
    -> std::expected<void, typename D::error_type> {
    KOTA_EXPECTED_TRY_V(auto seq, d.deserialize_seq(std::nullopt));

    value.clear();
    while(true) {
        KOTA_EXPECTED_TRY_V(auto has_next, seq.has_next());
        if(!has_next) {
            break;
        }

        std::uint64_t byte = 0;
        KOTA_EXPECTED_TRY(seq.deserialize_element(byte));
        if(byte > static_cast<std::uint64_t>((std::numeric_limits<std::uint8_t>::max)())) {
            return std::unexpected(D::error_type::number_out_of_range);
        }

        value.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(byte)));
    }

    return seq.end();
}

}  // namespace kota::codec::detail
