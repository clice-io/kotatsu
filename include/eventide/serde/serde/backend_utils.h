#pragma once

#include <cstdint>
#include <expected>
#include <limits>
#include <vector>

#include "eventide/serde/serde/traits.h"

namespace eventide::serde::detail {

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
            return std::unexpected(D::error_type::number_out_of_range);
        }

        value.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(byte)));
    }

    return seq->end();
}

}  // namespace eventide::serde::detail
