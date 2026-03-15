#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "eventide/common/expected_try.h"
#include "eventide/serde/bincode/error.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/serde/serde.h"

namespace eventide::serde::bincode {

template <typename Config = config::default_config>
class Serializer {
public:
    using config_type = Config;
    using value_type = void;
    using error_type = error_kind;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    class SerializeElements {
    public:
        SerializeElements(Serializer& serializer, std::size_t expected_count) noexcept :
            serializer(serializer), expected_count(expected_count) {}

        template <typename T>
        status_t serialize_element(const T& value) {
            if(written_count >= expected_count) {
                return serializer.mark_invalid(error_type::invalid_state);
            }

            ET_EXPECTED_TRY(serde::serialize(serializer, value));

            ++written_count;
            return {};
        }

        template <typename K, typename V>
        status_t serialize_entry(const K& key, const V& value) {
            if(written_count >= expected_count) {
                return serializer.mark_invalid(error_type::invalid_state);
            }

            ET_EXPECTED_TRY(serde::serialize(serializer, key));
            ET_EXPECTED_TRY(serde::serialize(serializer, value));

            ++written_count;
            return {};
        }

        template <typename T>
        status_t serialize_field(std::string_view /*key*/, const T& value) {
            return serialize_element(value);
        }

        result_t<value_type> end() {
            if(written_count != expected_count) {
                return serializer.mark_invalid(error_type::invalid_state);
            }
            return {};
        }

    private:
        Serializer& serializer;
        std::size_t expected_count = 0;
        std::size_t written_count = 0;
    };

    using SerializeSeq = SerializeElements;
    using SerializeTuple = SerializeElements;
    using SerializeMap = SerializeElements;
    using SerializeStruct = SerializeElements;

    Serializer() = default;

    explicit Serializer(std::size_t reserve_bytes) {
        bytes_buffer.reserve(reserve_bytes);
    }

    [[nodiscard]] bool valid() const noexcept {
        return is_valid;
    }

    [[nodiscard]] error_type error() const noexcept {
        return current_error();
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return std::span<const std::byte>(bytes_buffer.data(), bytes_buffer.size());
    }

    auto take_bytes() -> std::vector<std::byte> {
        return std::move(bytes_buffer);
    }

    result_t<value_type> serialize_null() {
        return write_u8(0);
    }

    template <typename T>
    result_t<value_type> serialize_some(const T& value) {
        ET_EXPECTED_TRY(write_u8(1));
        return serde::serialize(*this, value);
    }

    result_t<value_type> serialize_bool(bool value) {
        return write_u8(value ? 1 : 0);
    }

    result_t<value_type> serialize_int(std::int64_t value) {
        return write_integral(value);
    }

    result_t<value_type> serialize_uint(std::uint64_t value) {
        return write_integral(value);
    }

    result_t<value_type> serialize_float(double value) {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        return write_integral(bits);
    }

    result_t<value_type> serialize_char(char value) {
        return write_u8(static_cast<std::uint8_t>(value));
    }

    result_t<value_type> serialize_str(std::string_view value) {
        ET_EXPECTED_TRY(write_length(value.size()));

        if(!is_valid) {
            return std::unexpected(current_error());
        }

        if(value.empty()) {
            return {};
        }

        bytes_buffer.insert(bytes_buffer.end(),
                            reinterpret_cast<const std::byte*>(value.data()),
                            reinterpret_cast<const std::byte*>(value.data() + value.size()));
        return {};
    }

    result_t<value_type> serialize_bytes(std::span<const std::byte> value) {
        ET_EXPECTED_TRY(write_length(value.size()));

        if(!is_valid) {
            return std::unexpected(current_error());
        }

        bytes_buffer.insert(bytes_buffer.end(), value.begin(), value.end());
        return {};
    }

    template <typename... Ts>
    result_t<value_type> serialize_variant(const std::variant<Ts...>& value) {
        const auto variant_index = value.index();
        if(variant_index > (std::numeric_limits<std::uint32_t>::max)()) {
            return mark_invalid(error_type::invalid_variant_index);
        }

        ET_EXPECTED_TRY(write_integral(static_cast<std::uint32_t>(variant_index)));

        std::expected<void, error_type> payload_status{};
        std::visit(
            [&](const auto& item) {
                using item_t = std::remove_cvref_t<decltype(item)>;
                if constexpr(std::same_as<item_t, std::monostate>) {
                    payload_status = {};
                } else {
                    auto serialized = serde::serialize(*this, item);
                    if(!serialized) {
                        payload_status = std::unexpected(serialized.error());
                    } else {
                        payload_status = {};
                    }
                }
            },
            value);

        if(!payload_status) {
            return std::unexpected(payload_status.error());
        }
        return {};
    }

    result_t<SerializeSeq> serialize_seq(std::optional<std::size_t> len) {
        if(!len.has_value()) {
            return std::unexpected(error_type::invalid_state);
        }

        ET_EXPECTED_TRY(write_length(*len));
        return SerializeSeq(*this, *len);
    }

    result_t<SerializeTuple> serialize_tuple(std::size_t len) {
        return SerializeTuple(*this, len);
    }

    result_t<SerializeMap> serialize_map(std::optional<std::size_t> len) {
        if(!len.has_value()) {
            return std::unexpected(error_type::invalid_state);
        }

        ET_EXPECTED_TRY(write_length(*len));
        return SerializeMap(*this, *len);
    }

    result_t<SerializeStruct> serialize_struct(std::string_view /*name*/, std::size_t len) {
        return SerializeStruct(*this, len);
    }

private:
    template <typename T>
        requires std::integral<T>
    status_t write_integral(T value) {
        if(!is_valid) {
            return std::unexpected(current_error());
        }

        using unsigned_t = std::make_unsigned_t<T>;
        unsigned_t raw = static_cast<unsigned_t>(value);
        for(std::size_t i = 0; i < sizeof(unsigned_t); ++i) {
            const auto byte = static_cast<std::uint8_t>((raw >> (i * 8)) & 0xFFU);
            bytes_buffer.push_back(static_cast<std::byte>(byte));
        }
        return {};
    }

    status_t write_u8(std::uint8_t value) {
        return write_integral(value);
    }

    status_t write_length(std::size_t len) {
        if(static_cast<unsigned long long>(len) >
           static_cast<unsigned long long>((std::numeric_limits<std::uint64_t>::max)())) {
            return mark_invalid(error_type::invalid_state);
        }
        return write_integral(static_cast<std::uint64_t>(len));
    }

    status_t mark_invalid(error_type error) {
        is_valid = false;
        last_error = error;
        return std::unexpected(error);
    }

    error_type current_error() const {
        return is_valid ? error_type::ok : last_error;
    }

private:
    std::vector<std::byte> bytes_buffer;
    bool is_valid = true;
    error_type last_error = error_type::ok;
};

template <typename Config = config::default_config, typename T>
auto to_bytes(const T& value) -> std::expected<std::vector<std::byte>, error_kind> {
    Serializer<Config> serializer;
    ET_EXPECTED_TRY(serde::serialize(serializer, value));
    if(!serializer.valid()) {
        return std::unexpected(serializer.error());
    }
    return serializer.take_bytes();
}

static_assert(serde::serializer_like<Serializer<>>);

}  // namespace eventide::serde::bincode

namespace eventide::serde {

template <typename Config, typename T>
    requires is_specialization_of<std::variant, std::remove_cvref_t<T>>
struct serialize_traits<bincode::Serializer<Config>, T> {
    using serializer_t = bincode::Serializer<Config>;
    using error_type = typename serializer_t::error_type;

    static auto serialize(serializer_t& s, const T& value)
        -> std::expected<typename serializer_t::value_type, error_type> {
        return s.serialize_variant(value);
    }
};

}  // namespace eventide::serde
