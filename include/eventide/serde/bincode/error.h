#pragma once

#include <cstdint>
#include <string_view>

#include "eventide/serde/serde/error.h"

namespace eventide::serde::bincode {

enum class error_kind : std::uint8_t {
    ok = 0,
    invalid_state,
    unexpected_eof,
    type_mismatch,
    number_out_of_range,
    trailing_bytes,
    invalid_variant_index,
    unsupported_operation,
};

constexpr std::string_view error_message(error_kind error) {
    switch(error) {
        case error_kind::ok: return "ok";
        case error_kind::invalid_state: return "invalid_state";
        case error_kind::unexpected_eof: return "unexpected_eof";
        case error_kind::type_mismatch: return "type mismatch";
        case error_kind::number_out_of_range: return "number_out_of_range";
        case error_kind::trailing_bytes: return "trailing_bytes";
        case error_kind::invalid_variant_index: return "invalid_variant_index";
        case error_kind::unsupported_operation: return "unsupported_operation";
    }

    return "invalid_state";
}

using error = eventide::serde::serde_error<error_kind>;

}  // namespace eventide::serde::bincode
