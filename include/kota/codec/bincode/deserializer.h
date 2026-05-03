#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/codec/deserialize.h"
#include "kota/codec/bincode/backend.h"
#include "kota/codec/bincode/error.h"

namespace kota::codec::bincode {

template <typename Config = config::default_config, typename T>
auto from_bytes(std::span<const std::byte> bytes, T& value) -> std::expected<void, error> {
    using Backend = bincode_backend_with_config<Config>;
    byte_reader reader{bytes, 0};
    typename Backend::value_type src = &reader;
    auto err = codec::deserialize<Backend>(src, value);
    if(err != error_kind::ok) {
        return std::unexpected(error(err));
    }
    if(reader.offset != bytes.size()) {
        return std::unexpected(error(error_kind::trailing_bytes));
    }
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

}  // namespace kota::codec::bincode
