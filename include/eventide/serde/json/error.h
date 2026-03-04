#pragma once

#include <cstdint>
#include <string_view>

#if __has_include(<simdjson.h>)
#include <simdjson.h>
#define EVENTIDE_SERDE_JSON_ERROR_HAS_SIMDJSON 1
#else
#define EVENTIDE_SERDE_JSON_ERROR_HAS_SIMDJSON 0
#endif

#if __has_include(<yyjson.h>)
#include <yyjson.h>
#define EVENTIDE_SERDE_JSON_ERROR_HAS_YYJSON 1
#else
#define EVENTIDE_SERDE_JSON_ERROR_HAS_YYJSON 0
#endif

namespace eventide::serde::json {

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

#if EVENTIDE_SERDE_JSON_ERROR_HAS_SIMDJSON
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
        default: return error_kind::unknown;
    }
}

constexpr auto to_simdjson_error(error_kind error) noexcept -> simdjson::error_code {
    switch(error) {
        case error_kind::ok: return simdjson::SUCCESS;
        case error_kind::allocation_failed: return simdjson::MEMALLOC;
        case error_kind::type_mismatch: return simdjson::INCORRECT_TYPE;
        case error_kind::number_out_of_range: return simdjson::NUMBER_OUT_OF_RANGE;
        case error_kind::trailing_content: return simdjson::TRAILING_CONTENT;
        case error_kind::io_error: return simdjson::IO_ERROR;
        case error_kind::index_out_of_bounds: return simdjson::INDEX_OUT_OF_BOUNDS;
        case error_kind::no_such_field: return simdjson::NO_SUCH_FIELD;
        case error_kind::already_exists: return simdjson::NO_SUCH_FIELD;
        case error_kind::parse_error:
        case error_kind::write_failed:
        case error_kind::invalid_state:
        case error_kind::tape_error:
        case error_kind::unknown:
        default: return simdjson::TAPE_ERROR;
    }
}
#endif

#if EVENTIDE_SERDE_JSON_ERROR_HAS_YYJSON
constexpr auto make_read_error(yyjson_read_code error) noexcept -> error_kind {
#ifdef YYJSON_READ_SUCCESS
    if(error == YYJSON_READ_SUCCESS) {
        return error_kind::ok;
    }
#endif
    return error_kind::parse_error;
}

constexpr auto make_write_error(yyjson_write_code error) noexcept -> error_kind {
#ifdef YYJSON_WRITE_SUCCESS
    if(error == YYJSON_WRITE_SUCCESS) {
        return error_kind::ok;
    }
#endif
    return error_kind::write_failed;
}
#endif

}  // namespace eventide::serde::json
