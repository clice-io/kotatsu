#pragma once

#include <cstdint>
#include <expected>
#include <limits>
#include <type_traits>
#include <vector>

#include "eventide/serde/traits.h"

namespace eventide::serde::detail {

/// Produce an error for numeric values that exceed the target type's range.
/// Backends should use this instead of ad-hoc error construction.
template <typename Result>
constexpr Result unexpected_number_out_of_range() {
    using error_t = typename Result::error_type;
    if constexpr(requires { error_t::number_out_of_range; }) {
        return std::unexpected(error_t::number_out_of_range);
    } else if constexpr(requires { error_t::invalid_type; }) {
        return std::unexpected(error_t::invalid_type);
    } else if constexpr(std::is_enum_v<error_t>) {
        return std::unexpected(static_cast<error_t>(1));
    } else {
        return std::unexpected(error_t{});
    }
}

/// Produce an error for invalid enum string values.
template <typename Result>
constexpr Result unexpected_invalid_enum() {
    using error_t = typename Result::error_type;
    if constexpr(requires { error_t::invalid_type; }) {
        return std::unexpected(error_t::invalid_type);
    } else if constexpr(std::is_enum_v<error_t>) {
        return std::unexpected(static_cast<error_t>(1));
    } else {
        return std::unexpected(error_t{});
    }
}

/// Shared implementation: deserialize bytes as a sequence of uint8 values.
/// Used by JSON (simd, yy) and TOML backends that represent bytes as arrays.
template <deserializer_like D>
auto deserialize_bytes_from_seq(D& d, std::vector<std::byte>& value)
    -> std::expected<void, typename D::error_type> {
    auto seq = d.deserialize_seq(std::nullopt);
    if(!seq) {
        return std::unexpected(seq.error());
    }

    value.clear();
    while(true) {
        auto has_next = seq->has_next();
        if(!has_next) {
            return std::unexpected(has_next.error());
        }
        if(!*has_next) {
            break;
        }

        std::uint64_t byte = 0;
        auto byte_status = seq->deserialize_element(byte);
        if(!byte_status) {
            return std::unexpected(byte_status.error());
        }
        if(byte > static_cast<std::uint64_t>((std::numeric_limits<std::uint8_t>::max)())) {
            return unexpected_number_out_of_range<std::expected<void, typename D::error_type>>();
        }

        value.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(byte)));
    }

    return seq->end();
}

}  // namespace eventide::serde::detail
