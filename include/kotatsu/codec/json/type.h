#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "kotatsu/codec/context.h"
#include "kotatsu/codec/format.h"
#include "kotatsu/codec/json/format.h"

#if __has_include(<simdjson.h>)
// In a debug build simdjson's development checks return from its iterators'
// begin() ahead of the release return, which MSVC's code generator reports
// as unreachable (C4702); /external:W0 does not reach that warning.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
#include "simdjson.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#else
#error "simdjson.h is required for the JSON codec backend"
#endif

namespace kotatsu::codec::json {

namespace ondemand {

using Parser = simdjson::ondemand::parser;
using Document = simdjson::ondemand::document;
using Value = simdjson::ondemand::value;
using Type = simdjson::ondemand::json_type;
using NumberType = simdjson::ondemand::number_type;

}  // namespace ondemand

using padded_string = simdjson::padded_string;
using padded_string_view = simdjson::padded_string_view;

constexpr inline auto success = simdjson::SUCCESS;

namespace detail {

/// simdjson's message for an error code, as a rich_error.
inline rich_error simdjson_error(simdjson::error_code ec) {
    return rich_error(std::string(simdjson::error_message(ec)));
}

}  // namespace detail

}  // namespace kotatsu::codec::json
