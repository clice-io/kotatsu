#pragma once

#include <bit>
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
#include <variant>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/support/ranges.h"
#include "kota/codec/backend.h"
#include "kota/codec/bincode/error.h"
#include "kota/codec/codec.h"
#include "kota/codec/config.h"
#include "kota/codec/detail/narrow.h"

namespace kota::codec::bincode {

template <typename Config = config::default_config>
class Deserializer {
public:
    using config_type = Config;
    using error_type = bincode::error;

    constexpr static auto backend_kind_v = backend_kind::streaming;
    constexpr static auto field_mode_v = field_mode::by_position;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    explicit Deserializer(std::span<const std::byte> bytes) : bytes(bytes) {}

    explicit Deserializer(std::span<const std::uint8_t> bytes) :
        bytes(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()) {}

    explicit Deserializer(const std::vector<std::byte>& bytes) :
        Deserializer(std::span<const std::byte>(bytes.data(), bytes.size())) {}

    explicit Deserializer(const std::vector<std::uint8_t>& bytes) :
        Deserializer(std::span<const std::uint8_t>(bytes.data(), bytes.size())) {}

    [[nodiscard]] bool valid() const noexcept {
        return is_valid;
    }

    [[nodiscard]] error_type error() const noexcept {
        return last_error;
    }

    status_t finish() {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        if(offset != bytes.size()) {
            return mark_invalid(error_kind::trailing_bytes);
        }
        return {};
    }

    status_t deserialize_bool(bool& value) {
        KOTA_EXPECTED_TRY_V(auto parsed, read_u8());

        if(parsed > 1U) {
            return mark_invalid(error_type::type_mismatch);
        }
        value = parsed == 1U;
        return {};
    }

    template <codec::int_like T>
    status_t deserialize_int(T& value) {
        KOTA_EXPECTED_TRY_V(auto parsed, read_integral<std::int64_t>());

        auto narrowed = codec::detail::narrow_int<T>(parsed, error_type::number_out_of_range);
        if(!narrowed) {
            return mark_invalid(narrowed.error());
        }
        value = *narrowed;
        return {};
    }

    template <codec::uint_like T>
    status_t deserialize_uint(T& value) {
        KOTA_EXPECTED_TRY_V(auto parsed, read_integral<std::uint64_t>());

        auto narrowed = codec::detail::narrow_uint<T>(parsed, error_type::number_out_of_range);
        if(!narrowed) {
            return mark_invalid(narrowed.error());
        }
        value = *narrowed;
        return {};
    }

    template <codec::floating_like T>
    status_t deserialize_float(T& value) {
        KOTA_EXPECTED_TRY_V(auto raw, read_integral<std::uint64_t>());

        const double parsed = std::bit_cast<double>(raw);
        auto narrowed = codec::detail::narrow_float<T>(parsed, error_type::number_out_of_range);
        if(!narrowed) {
            return mark_invalid(narrowed.error());
        }
        value = *narrowed;
        return {};
    }

    status_t deserialize_char(char& value) {
        KOTA_EXPECTED_TRY_V(auto parsed, read_u8());
        value = static_cast<char>(parsed);
        return {};
    }

    status_t deserialize_str(std::string& value) {
        KOTA_EXPECTED_TRY_V(auto length, read_length());

        if(offset + length > bytes.size()) {
            return mark_invalid(error_kind::unexpected_eof);
        }

        if(length == 0) {
            value.clear();
            return {};
        }

        const auto* begin = reinterpret_cast<const char*>(bytes.data() + offset);
        value.assign(begin, begin + length);
        offset += length;
        return {};
    }

    status_t deserialize_bytes(std::vector<std::byte>& value) {
        KOTA_EXPECTED_TRY_V(auto length, read_length());

        if(offset + length > bytes.size()) {
            return mark_invalid(error_kind::unexpected_eof);
        }

        if(length == 0) {
            value.clear();
            return {};
        }

        value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                     bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
        return {};
    }

    result_t<bool> deserialize_none() {
        KOTA_EXPECTED_TRY_V(auto tag, read_u8());

        if(tag == 0U) {
            return true;
        }
        if(tag == 1U) {
            return false;
        }
        return mark_invalid(error_type::type_mismatch);
    }

    template <typename... Ts>
    status_t deserialize_variant(std::variant<Ts...>& value) {
        KOTA_EXPECTED_TRY_V(auto index, read_integral<std::uint32_t>());

        constexpr std::size_t variant_size = sizeof...(Ts);
        if(index >= variant_size) {
            return mark_invalid(error_kind::invalid_variant_index);
        }

        std::expected<void, error_type> status{};
        bool matched = false;

        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&] {
                 if(index != I) {
                     return;
                 }

                 matched = true;
                 status = deserialize_variant_alternative<I>(value);
             }()),
             ...);
        }(std::make_index_sequence<variant_size>{});

        if(!matched) {
            return mark_invalid(error_kind::invalid_variant_index);
        }
        if(!status) {
            return std::unexpected(status.error());
        }
        return {};
    }

    // --- Streaming object interface (unsupported for binary format) ---

    status_t begin_object() {
        return mark_invalid(error_kind::unsupported_operation);
    }

    status_t end_object() {
        return mark_invalid(error_kind::unsupported_operation);
    }

    result_t<std::optional<std::string_view>> next_field() {
        return mark_invalid(error_kind::unsupported_operation);
    }

    status_t skip_field_value() {
        return mark_invalid(error_kind::unsupported_operation);
    }

    // --- Streaming array interface ---

    status_t begin_array() {
        KOTA_EXPECTED_TRY_V(auto len, read_length());
        array_stack.push_back(len);
        return {};
    }

    result_t<bool> next_element() {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        if(array_stack.empty() || array_stack.back() == 0) {
            return false;
        }
        --array_stack.back();
        return true;
    }

    status_t end_array() {
        if(!array_stack.empty()) {
            array_stack.pop_back();
        }
        return {};
    }

private:
    template <std::size_t I, typename Variant>
    status_t deserialize_variant_alternative(Variant& value) {
        using alt_t = std::variant_alternative_t<I, Variant>;
        if constexpr(std::same_as<alt_t, std::monostate>) {
            value = std::monostate{};
            return {};
        } else if constexpr(std::default_initializable<alt_t>) {
            alt_t alt{};
            KOTA_EXPECTED_TRY(codec::deserialize(*this, alt));

            value = std::move(alt);
            return {};
        } else {
            return mark_invalid(error_type::invalid_state);
        }
    }

    template <typename T>
        requires std::integral<T>
    result_t<T> read_integral() {
        if(!is_valid) {
            return std::unexpected(last_error);
        }

        using unsigned_t = std::make_unsigned_t<T>;
        if(offset + sizeof(unsigned_t) > bytes.size()) {
            return mark_invalid(error_kind::unexpected_eof);
        }

        unsigned_t raw = 0;
        for(std::size_t i = 0; i < sizeof(unsigned_t); ++i) {
            const auto byte = std::to_integer<std::uint8_t>(bytes[offset + i]);
            raw |= (static_cast<unsigned_t>(byte) << (i * 8));
        }

        offset += sizeof(unsigned_t);
        if constexpr(std::signed_integral<T>) {
            return std::bit_cast<T>(raw);
        } else {
            return static_cast<T>(raw);
        }
    }

    result_t<std::uint8_t> read_u8() {
        return read_integral<std::uint8_t>();
    }

    result_t<std::size_t> read_length() {
        KOTA_EXPECTED_TRY_V(auto raw, read_integral<std::uint64_t>());

        if(raw > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
            return mark_invalid(error_type::number_out_of_range);
        }

        return static_cast<std::size_t>(raw);
    }

    std::unexpected<error_type> mark_invalid(error_type error) {
        is_valid = false;
        last_error = error;
        return std::unexpected(last_error);
    }

private:
    std::span<const std::byte> bytes{};
    std::size_t offset = 0;
    std::vector<std::size_t> array_stack;
    bool is_valid = true;
    error_type last_error = error_kind::ok;
};

template <typename Config = config::default_config, typename T>
auto from_bytes(std::span<const std::byte> bytes, T& value) -> std::expected<void, error> {
    Deserializer<Config> deserializer(bytes);
    if(!deserializer.valid()) {
        return std::unexpected(deserializer.error());
    }

    KOTA_EXPECTED_TRY(codec::deserialize(deserializer, value));
    KOTA_EXPECTED_TRY(deserializer.finish());
    return {};
}

template <typename Config = config::default_config, typename T>
auto from_bytes(std::span<const std::uint8_t> bytes, T& value) -> std::expected<void, error> {
    return from_bytes<Config>(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()),
        value);
}

template <typename Config = config::default_config, typename T>
auto from_bytes(const std::vector<std::byte>& bytes, T& value) -> std::expected<void, error> {
    return from_bytes<Config>(std::span<const std::byte>(bytes.data(), bytes.size()), value);
}

template <typename Config = config::default_config, typename T>
auto from_bytes(const std::vector<std::uint8_t>& bytes, T& value) -> std::expected<void, error> {
    return from_bytes<Config>(std::span<const std::uint8_t>(bytes.data(), bytes.size()), value);
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_bytes(std::span<const std::byte> bytes) -> std::expected<T, error> {
    T value{};
    KOTA_EXPECTED_TRY(from_bytes<Config>(bytes, value));
    return value;
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_bytes(std::span<const std::uint8_t> bytes) -> std::expected<T, error> {
    T value{};
    KOTA_EXPECTED_TRY(from_bytes<Config>(bytes, value));
    return value;
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_bytes(const std::vector<std::byte>& bytes) -> std::expected<T, error> {
    return from_bytes<T, Config>(std::span<const std::byte>(bytes.data(), bytes.size()));
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_bytes(const std::vector<std::uint8_t>& bytes) -> std::expected<T, error> {
    return from_bytes<T, Config>(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
}

static_assert(codec::deserializer_like<Deserializer<>>);

}  // namespace kota::codec::bincode

namespace kota::codec {

template <typename Config, typename T>
    requires (std::ranges::input_range<std::remove_cvref_t<T>> &&
              format_kind<std::remove_cvref_t<T>> == range_format::map)
struct deserialize_traits<bincode::Deserializer<Config>, T> {
    using deserializer_t = bincode::Deserializer<Config>;
    using error_type = typename deserializer_t::error_type;
    using map_t = std::remove_cvref_t<T>;
    using key_t = typename map_t::key_type;
    using mapped_t = typename map_t::mapped_type;

    static auto deserialize(deserializer_t& deserializer, T& value)
        -> std::expected<void, error_type> {
        static_assert(std::default_initializable<key_t>,
                      "bincode map deserialization requires default-constructible key_type");
        static_assert(std::default_initializable<mapped_t>,
                      "bincode map deserialization requires default-constructible mapped_type");
        static_assert(kota::detail::map_insertable<map_t, key_t, mapped_t>,
                      "bincode map deserialization requires insertable map container");

        KOTA_EXPECTED_TRY(deserializer.begin_array());

        if constexpr(requires { value.clear(); }) {
            value.clear();
        }

        while(true) {
            KOTA_EXPECTED_TRY_V(auto has_next, deserializer.next_element());
            if(!has_next) {
                break;
            }

            key_t key{};
            KOTA_EXPECTED_TRY(codec::deserialize(deserializer, key));

            mapped_t mapped{};
            KOTA_EXPECTED_TRY(codec::deserialize(deserializer, mapped));

            kota::detail::insert_map_entry(value, std::move(key), std::move(mapped));
        }

        return deserializer.end_array();
    }
};

}  // namespace kota::codec
