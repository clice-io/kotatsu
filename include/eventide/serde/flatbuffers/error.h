#pragma once

#include <cstdint>
#include <string_view>

namespace eventide::serde::flex {

enum class error_code : std::uint8_t {
    unknown = 0,
    none,
    invalid_state,
    invalid_buffer,
    type_mismatch,
    number_out_of_range,
    invalid_char,
    invalid_key,
    root_not_consumed,
    duplicate_keys,
};

constexpr std::string_view error_message(error_code code) {
    switch(code) {
        case error_code::none: return "none";
        case error_code::unknown: return "unknown";
        case error_code::invalid_state: return "invalid_state";
        case error_code::invalid_buffer: return "invalid_buffer";
        case error_code::type_mismatch: return "type mismatch";
        case error_code::number_out_of_range: return "number_out_of_range";
        case error_code::invalid_char: return "invalid_char";
        case error_code::invalid_key: return "invalid_key";
        case error_code::root_not_consumed: return "root_not_consumed";
        case error_code::duplicate_keys: return "duplicate_keys";
    }
    return "unknown";
}

}  // namespace eventide::serde::flex
