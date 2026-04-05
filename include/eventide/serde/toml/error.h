#pragma once

#include <cstdint>
#include <string_view>

#include "eventide/serde/serde/error.h"

namespace eventide::serde::toml {

enum class error_kind : std::uint16_t {
    ok = 0,
    invalid_state,
    parse_error,
    type_mismatch,
    number_out_of_range,
    unsupported_type,
    trailing_content,
    unknown,
};

constexpr auto error_message(error_kind error) noexcept -> std::string_view {
    switch(error) {
        case error_kind::ok: return "success";
        case error_kind::invalid_state: return "invalid state";
        case error_kind::parse_error: return "parse error";
        case error_kind::type_mismatch: return "type mismatch";
        case error_kind::number_out_of_range: return "number out of range";
        case error_kind::unsupported_type: return "unsupported type";
        case error_kind::trailing_content: return "trailing content";
        case error_kind::unknown:
        default: return "unknown toml error";
    }
}

using error = eventide::serde::serde_error<error_kind>;

}  // namespace eventide::serde::toml
