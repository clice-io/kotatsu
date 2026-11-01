#pragma once

#include <cstddef>
#include <optional>

#include "kotatsu/codec/format.h"

// The JSON backend's format and options, apart from the backend itself, which
// needs simdjson: a format of your own can extend json::format where the
// backend is not built.

namespace kotatsu::codec::json {

/// The JSON backend's format, which a format of your own extends to say what
/// it changes for JSON (codec::extend), and which scopes a repr to JSON
/// (codec::repr<T, codec::json::format>).
///
/// # Lowerings
///
/// Human-readable text backend (simdjson ondemand parse, StringBuilder
/// write). How each meta::type_kind lands in JSON:
/// - null, empty optional, null pointer → `null`; engaged optional/pointer →
///   the payload itself, no wrapper
/// - boolean → `true` / `false`
/// - integers → JSON numbers; uint64 is emitted full-range, beyond 2^53,
///   which JavaScript's JSON.parse rounds to the nearest double: a reader in
///   JavaScript needs a lossless parser (BigInt) for such values. Decode
///   reads integer tokens only: `1.0`, an integer to JSON Schema, does not
///   read into an integer
/// - float32/float64 → JSON numbers; one that is not finite fails to
///   encode, JSON having no number for NaN or an infinity
/// - character → single-codepoint string (the char's value 0-255 encoded as
///   UTF-8); decode accepts exactly one codepoint ≤ 255
/// - string → quoted, escaped string; one that is not UTF-8 follows the
///   encode's invalid_utf8
/// - bytes → array of octet numbers
/// - enumeration → underlying integer; through codec::enum_names, the
///   enumerator's name
/// - array/set/tuple → `[...]`
/// - map → object; non-string keys go through MapKeyWriter/MapKeyReader as
///   their decimal string form
/// - structure → object keyed by (renamed) field names
/// - variant → shaped by the spec's tag_mode (see encode_tagged_variant in
///   encode.h); untagged variants emit the bare payload
/// - raw_value → spliced into the output verbatim; an empty raw_value is `null`
struct format : codec::extend<codec::format> {
    /// Its documents are UTF-8 text.
    constexpr static bool utf8 = true;
};

/// How one encode to JSON goes, decided at run time: codec::encode_options,
/// and the room the text starts with. Its members are its own, so that
/// designated initializers name any of them.
struct encode_options {
    /// What a string that is not UTF-8 makes of the encode.
    codec::invalid_utf8 invalid_utf8 = codec::invalid_utf8::Error;
    /// The room the text starts with; unset, 1 KiB.
    std::optional<std::size_t> initial_capacity = {};

    /// The options every backend's encode takes.
    constexpr codec::encode_options common() const {
        return {.invalid_utf8 = invalid_utf8};
    }
};

}  // namespace kotatsu::codec::json
