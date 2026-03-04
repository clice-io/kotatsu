#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "eventide/serde/flatbuffers/error.h"
#include "eventide/serde/serde.h"

#if __has_include(<flatbuffers/flexbuffers.h>)
#include <flatbuffers/flexbuffers.h>
#else
#error                                                                                             \
    "flatbuffers/flexbuffers.h not found. Enable EVENTIDE_SERDE_ENABLE_FLATBUFFERS or add flatbuffers include paths."
#endif

namespace eventide::serde::flex {

class Deserializer {
public:
    using error_type = error_code;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    class DeserializeArray {
    public:
        result_t<bool> has_next();

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

            auto parsed = deserializer.deserialize_from_reference(vector[consumed_count], value);
            if(!parsed) {
                return std::unexpected(parsed.error());
            }

            ++consumed_count;
            return {};
        }

        status_t skip_element();
        status_t end();

    private:
        friend class Deserializer;

        DeserializeArray(Deserializer& deserializer,
                         ::flexbuffers::Vector vector,
                         std::size_t expected_length,
                         bool is_strict_length);

        Deserializer& deserializer;
        ::flexbuffers::Vector vector;
        std::size_t expected_length = 0;
        std::size_t consumed_count = 0;
        bool is_strict_length = false;
    };

    class DeserializeObject {
    public:
        result_t<std::optional<std::string_view>> next_key();
        status_t invalid_key(std::string_view key_name);

        template <typename T>
        status_t deserialize_value(T& value) {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.current_error());
            }
            if(!has_pending_value) {
                deserializer.mark_invalid();
                return std::unexpected(deserializer.current_error());
            }

            auto parsed = deserializer.deserialize_from_reference(pending_value, value);
            if(!parsed) {
                return std::unexpected(parsed.error());
            }

            ++index;
            has_pending_value = false;
            return {};
        }

        status_t skip_value();
        status_t end();

    private:
        friend class Deserializer;

        DeserializeObject(Deserializer& deserializer, ::flexbuffers::Map map);

        Deserializer& deserializer;
        ::flexbuffers::Map map;
        ::flexbuffers::TypedVector keys;
        ::flexbuffers::Vector values;
        std::size_t index = 0;
        ::flexbuffers::Reference pending_value{};
        std::string pending_key;
        bool has_pending_value = false;
    };

    using DeserializeSeq = DeserializeArray;
    using DeserializeTuple = DeserializeArray;
    using DeserializeMap = DeserializeObject;
    using DeserializeStruct = DeserializeObject;

    explicit Deserializer(std::span<const std::uint8_t> bytes);
    explicit Deserializer(std::span<const std::byte> bytes);
    explicit Deserializer(const std::vector<std::uint8_t>& bytes);

    bool valid() const;
    error_type error() const;

    status_t finish();

    result_t<bool> deserialize_none();

    template <typename... Ts>
    status_t deserialize_variant(std::variant<Ts...>& value) {
        /// FIXME: This is wrong implementation.
        return std::visit([&](auto& item) { return serde::deserialize(*this, item); }, value);
    }

    status_t deserialize_bool(bool& value);

    template <serde::int_like T>
    status_t deserialize_int(T& value) {
        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }

        std::int64_t parsed = 0;
        if(reference->IsInt()) {
            parsed = reference->AsInt64();
        } else if(reference->IsUInt()) {
            const std::uint64_t raw = reference->AsUInt64();
            if(!std::in_range<std::int64_t>(raw)) {
                mark_invalid(error_code::number_out_of_range);
                return std::unexpected(current_error());
            }
            parsed = static_cast<std::int64_t>(raw);
        } else {
            mark_invalid(error_code::invalid_type);
            return std::unexpected(current_error());
        }

        if(!std::in_range<T>(parsed)) {
            mark_invalid(error_code::number_out_of_range);
            return std::unexpected(current_error());
        }

        value = static_cast<T>(parsed);
        consume_root_if_needed();
        return {};
    }

    template <serde::uint_like T>
    status_t deserialize_uint(T& value) {
        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }

        std::uint64_t parsed = 0;
        if(reference->IsUInt()) {
            parsed = reference->AsUInt64();
        } else if(reference->IsInt()) {
            const std::int64_t raw = reference->AsInt64();
            if(raw < 0) {
                mark_invalid(error_code::number_out_of_range);
                return std::unexpected(current_error());
            }
            parsed = static_cast<std::uint64_t>(raw);
        } else {
            mark_invalid(error_code::invalid_type);
            return std::unexpected(current_error());
        }

        if(!std::in_range<T>(parsed)) {
            mark_invalid(error_code::number_out_of_range);
            return std::unexpected(current_error());
        }

        value = static_cast<T>(parsed);
        consume_root_if_needed();
        return {};
    }

    template <serde::floating_like T>
    status_t deserialize_float(T& value) {
        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }
        if(!reference->IsNumeric()) {
            mark_invalid(error_code::invalid_type);
            return std::unexpected(current_error());
        }

        const double parsed = reference->AsDouble();
        if constexpr(!std::same_as<T, double>) {
            if(std::isfinite(parsed)) {
                const auto low = static_cast<long double>((std::numeric_limits<T>::lowest)());
                const auto high = static_cast<long double>((std::numeric_limits<T>::max)());
                const auto v = static_cast<long double>(parsed);
                if(v < low || v > high) {
                    mark_invalid(error_code::number_out_of_range);
                    return std::unexpected(current_error());
                }
            }
        }

        value = static_cast<T>(parsed);
        consume_root_if_needed();
        return {};
    }

    status_t deserialize_char(char& value);
    status_t deserialize_str(std::string& value);
    status_t deserialize_bytes(std::vector<std::byte>& value);

    result_t<DeserializeSeq> deserialize_seq(std::optional<std::size_t> len);
    result_t<DeserializeTuple> deserialize_tuple(std::size_t len);
    result_t<DeserializeMap> deserialize_map(std::optional<std::size_t> len);
    result_t<DeserializeStruct> deserialize_struct(std::string_view name, std::size_t len);

private:
    struct value_scope {
        value_scope(Deserializer& deserializer, const ::flexbuffers::Reference& reference);
        ~value_scope();

        Deserializer& deserializer;
        const ::flexbuffers::Reference* previous;
        ::flexbuffers::Reference value;
    };

    template <typename T>
    status_t deserialize_from_reference(const ::flexbuffers::Reference& reference, T& out) {
        value_scope scope(*this, reference);
        return serde::deserialize(*this, out);
    }

    result_t<::flexbuffers::Reference> active_reference();

    void consume_root_if_needed();
    void initialize();

    void set_error(error_type error);
    void mark_invalid(error_type error = error_code::invalid_state);
    error_type current_error() const;

private:
    bool is_valid = true;
    bool root_consumed = false;
    error_type last_error = error_code::none;
    const ::flexbuffers::Reference* current_value = nullptr;

    std::vector<std::uint8_t> buffer_storage;
    ::flexbuffers::Reference root_reference{};
};

template <typename T>
auto from_flatbuffer(std::span<const std::uint8_t> bytes, T& value)
    -> std::expected<void, error_code> {
    Deserializer deserializer(bytes);
    if(!deserializer.valid()) {
        return std::unexpected(deserializer.error());
    }

    auto result = serde::deserialize(deserializer, value);
    if(!result) {
        return std::unexpected(result.error());
    }

    return deserializer.finish();
}

template <typename T>
auto from_flatbuffer(std::span<const std::byte> bytes, T& value)
    -> std::expected<void, error_code> {
    if(bytes.empty()) {
        return from_flatbuffer(std::span<const std::uint8_t>{}, value);
    }
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    return from_flatbuffer(std::span<const std::uint8_t>(data, bytes.size()), value);
}

template <typename T>
auto from_flatbuffer(const std::vector<std::uint8_t>& bytes, T& value)
    -> std::expected<void, error_code> {
    return from_flatbuffer(std::span<const std::uint8_t>(bytes.data(), bytes.size()), value);
}

template <typename T>
    requires std::default_initializable<T>
auto from_flatbuffer(std::span<const std::uint8_t> bytes) -> std::expected<T, error_code> {
    T value{};
    auto status = from_flatbuffer(bytes, value);
    if(!status) {
        return std::unexpected(status.error());
    }
    return value;
}

template <typename T>
    requires std::default_initializable<T>
auto from_flatbuffer(std::span<const std::byte> bytes) -> std::expected<T, error_code> {
    if(bytes.empty()) {
        return from_flatbuffer<T>(std::span<const std::uint8_t>{});
    }
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    return from_flatbuffer<T>(std::span<const std::uint8_t>(data, bytes.size()));
}

template <typename T>
    requires std::default_initializable<T>
auto from_flatbuffer(const std::vector<std::uint8_t>& bytes) -> std::expected<T, error_code> {
    return from_flatbuffer<T>(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
}

static_assert(serde::deserializer_like<Deserializer>);

}  // namespace eventide::serde::flex
