#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string_view>
#include <vector>

namespace eventide::serde::detail {

/// Generic index-based array deserializer for DOM-like backends.
///
/// Requirements on D (parent Deserializer):
///   - D::error_type, D::result_t<T>, D::status_t
///   - d.valid() -> bool
///   - d.current_error() -> error_type
///   - d.mark_invalid() / d.mark_invalid(error)
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
            return std::unexpected(deserializer.current_error());
        }
        return index < array_size;
    }

    template <typename T>
    status_t deserialize_element(T& value) {
        auto has_next_result = has_next();
        if(!has_next_result) {
            return std::unexpected(has_next_result.error());
        }
        if(!*has_next_result) {
            deserializer.mark_invalid();
            return std::unexpected(deserializer.current_error());
        }

        auto status = deserializer.deserialize_element_value(array, index, value);
        if(!status) {
            return std::unexpected(status.error());
        }

        ++index;
        ++consumed_count;
        return {};
    }

    status_t skip_element() {
        auto has_next_result = has_next();
        if(!has_next_result) {
            return std::unexpected(has_next_result.error());
        }
        if(!*has_next_result) {
            deserializer.mark_invalid();
            return std::unexpected(deserializer.current_error());
        }

        ++index;
        ++consumed_count;
        return {};
    }

    status_t end() {
        if(!deserializer.valid()) {
            return std::unexpected(deserializer.current_error());
        }

        auto has_next_result = has_next();
        if(!has_next_result) {
            return std::unexpected(has_next_result.error());
        }

        if(strict_length) {
            if(consumed_count != expected_length || *has_next_result) {
                deserializer.mark_invalid();
                return std::unexpected(deserializer.current_error());
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
///   - d.current_error() -> error_type
///   - d.mark_invalid() / d.mark_invalid(error)
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
            return std::unexpected(deserializer.current_error());
        }
        if(has_pending_value) {
            deserializer.mark_invalid();
            return std::unexpected(deserializer.current_error());
        }
        if(index == entries.size()) {
            return std::optional<std::string_view>{};
        }

        has_pending_value = true;
        return std::optional<std::string_view>{entries[index].key};
    }

    status_t invalid_key(std::string_view /*key_name*/) {
        if(!deserializer.valid()) {
            return std::unexpected(deserializer.current_error());
        }
        if(!has_pending_value) {
            deserializer.mark_invalid();
            return std::unexpected(deserializer.current_error());
        }

        ++index;
        has_pending_value = false;
        return {};
    }

    template <typename T>
    status_t deserialize_value(T& value) {
        if(!deserializer.valid()) {
            return std::unexpected(deserializer.current_error());
        }
        if(!has_pending_value) {
            deserializer.mark_invalid();
            return std::unexpected(deserializer.current_error());
        }

        auto status = deserializer.deserialize_entry_value(entries[index].value, value);
        if(!status) {
            return std::unexpected(status.error());
        }

        ++index;
        has_pending_value = false;
        return {};
    }

    status_t skip_value() {
        if(!deserializer.valid()) {
            return std::unexpected(deserializer.current_error());
        }
        if(!has_pending_value) {
            deserializer.mark_invalid();
            return std::unexpected(deserializer.current_error());
        }

        ++index;
        has_pending_value = false;
        return {};
    }

    status_t end() {
        if(!deserializer.valid()) {
            return std::unexpected(deserializer.current_error());
        }

        if(has_pending_value) {
            auto status = skip_value();
            if(!status) {
                return std::unexpected(status.error());
            }
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

}  // namespace eventide::serde::detail
