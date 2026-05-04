#pragma once

#include <cstdint>
#include <string_view>

#include "kota/codec/detail/error.h"

#if __has_include(<simdjson.h>)
#include "simdjson.h"
#define KOTA_CODEC_JSON_ERROR_HAS_SIMDJSON 1
#else
#define KOTA_CODEC_JSON_ERROR_HAS_SIMDJSON 0
#endif

namespace kota::codec::json {

enum class error_kind : std::uint16_t {
    ok = 0,
    invalid_state,
    parse_error,
    write_failed,
    allocation_failed,
    type_mismatch,
    number_out_of_range,
    trailing_content,
    io_error,
    tape_error,
    index_out_of_bounds,
    no_such_field,
    already_exists,
    unknown,
};

constexpr auto error_message(error_kind error) noexcept -> std::string_view {
    switch(error) {
        case error_kind::ok: return "success";
        case error_kind::invalid_state: return "invalid state";
        case error_kind::parse_error: return "parse error";
        case error_kind::write_failed: return "write failed";
        case error_kind::allocation_failed: return "allocation failed";
        case error_kind::type_mismatch: return "type mismatch";
        case error_kind::number_out_of_range: return "number out of range";
        case error_kind::trailing_content: return "trailing content";
        case error_kind::io_error: return "I/O error";
        case error_kind::tape_error: return "tape error";
        case error_kind::index_out_of_bounds: return "index out of bounds";
        case error_kind::no_such_field: return "no such field";
        case error_kind::already_exists: return "already exists";
        case error_kind::unknown:
        default: return "unknown json error";
    }
}

#if KOTA_CODEC_JSON_ERROR_HAS_SIMDJSON
constexpr auto make_error(simdjson::error_code error) noexcept -> error_kind {
    switch(error) {
        case simdjson::SUCCESS: return error_kind::ok;
        case simdjson::MEMALLOC: return error_kind::allocation_failed;
        case simdjson::INCORRECT_TYPE: return error_kind::type_mismatch;
        case simdjson::NUMBER_OUT_OF_RANGE: return error_kind::number_out_of_range;
        case simdjson::TRAILING_CONTENT: return error_kind::trailing_content;
        case simdjson::IO_ERROR: return error_kind::io_error;
        case simdjson::INDEX_OUT_OF_BOUNDS: return error_kind::index_out_of_bounds;
        case simdjson::NO_SUCH_FIELD: return error_kind::no_such_field;
        case simdjson::TAPE_ERROR: return error_kind::tape_error;
        case simdjson::PARSER_IN_USE: return error_kind::invalid_state;
        default: return error_kind::unknown;
    }
}

#endif

using error = kota::codec::serde_error<error_kind>;

}  // namespace kota::codec::json
