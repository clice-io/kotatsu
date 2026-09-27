#pragma once

#include <string>

#include "kota/codec/visit/context.h"

#if __has_include(<simdjson.h>)
#include "simdjson.h"
#else
#error "simdjson.h is required for the JSON codec backend"
#endif

namespace kota::codec::json {

/// Format tag: scopes a meta::repr specialization to the JSON backend
/// (meta::repr<T, codec::json::format>).
///
/// # Lowerings
///
/// Human-readable text backend (simdjson ondemand parse / string_builder
/// write). How each meta::type_kind lands in JSON:
/// - null, empty optional, null pointer → `null`; engaged optional/pointer →
///   the payload itself, no wrapper
/// - boolean → `true` / `false`
/// - integers → JSON numbers; uint64 is emitted full-range, beyond 2^53
/// - float32/float64 → JSON numbers; non-finite values follow Config's
///   nan_repr, except that JSON has no non-finite literal, so Passthrough
///   also emits `null`
/// - character → single-codepoint string (the char's value 0-255 encoded as
///   UTF-8); decode accepts exactly one codepoint ≤ 255
/// - string → quoted, escaped string
/// - bytes → array of octet numbers
/// - enumeration → underlying integer, or the renamed enumerator name under
///   enum_repr::String
/// - array/set/tuple → `[...]`
/// - map → object; non-string keys go through MapKeyWriter/MapKeyReader as
///   their decimal string form
/// - structure → object keyed by (renamed) field names
/// - variant → shaped by the spec's tag_mode (see encode_tagged_variant in
///   visit/encode.h); untagged variants emit the bare payload
/// - RawValue → spliced into the output verbatim; an empty RawValue is `null`
struct format {};

using StringBuilder = simdjson::builder::string_builder;

namespace ondemand {

using Parser = simdjson::ondemand::parser;
using Document = simdjson::ondemand::document;
using Value = simdjson::ondemand::value;
using Type = simdjson::ondemand::json_type;
using NumberType = simdjson::ondemand::number_type;

}  // namespace ondemand

using padded_string = simdjson::padded_string;

constexpr inline auto success = simdjson::SUCCESS;

using error = rich_error;

namespace detail {

/// simdjson's message for an error code, as a rich_error.
inline rich_error simdjson_error(simdjson::error_code ec) {
    return rich_error(std::string(simdjson::error_message(ec)));
}

}  // namespace detail

}  // namespace kota::codec::json
