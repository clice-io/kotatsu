#pragma once

#include "kota/codec/visit/context.h"

namespace kota::codec::bincode {

/// Format tag: scopes a meta::repr specialization to the bincode backend
/// (meta::repr<T, codec::bincode::format>).
///
/// # Lowerings
///
/// Non-self-describing little-endian binary: no field names, no type tags,
/// and no markers beyond the prefixes listed below, so decode must run with
/// exactly the type and config that encoded the bytes. human_readable is
/// false, so variants are never name-tagged. Byte-level details live on
/// Writer (encode.h) and Reader (decode.h).
/// - null (and disengaged optional/pointer) → one 0x00 byte; decode rejects
///   other values. std::monostate, which has a single value, writes nothing
/// - boolean → one byte, 0 or 1; decode rejects other values
/// - integers → fixed 8-byte LE regardless of declared width; decode
///   range-checks when narrowing back
/// - float32/float64 (and long double) → the value as IEEE double, 8-byte
///   LE bit pattern
/// - character → one byte
/// - string / bytes → u64 LE byte-length prefix + raw bytes
/// - enumeration → underlying integer (8 bytes), or a length-prefixed name
///   string under enum_repr::String
/// - array/set/map → u64 LE element-count prefix, then elements back to
///   back (maps: key, value, key, value, ...)
/// - tuple/structure → fields concatenated in declaration order, no prefix;
///   skip_if omits nothing, since no marker could say a field is absent
/// - variant → u32 LE alternative index + payload
/// - optional/pointer → presence byte (0x00 / 0x01) + payload when engaged
/// - RawValue → stored as a bytes blob (length prefix + raw)
/// - dyn::Value → its ValueKind as one byte, then what it holds as above (an
///   array as a sequence of Values, an object as a map from strings to
///   Values); decode rejects an unknown kind and nesting deeper than 1024
struct format {};

}  // namespace kota::codec::bincode
