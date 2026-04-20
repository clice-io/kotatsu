#pragma once

namespace kota::codec {

/// The fundamental kind of backend determines which serialization model is used.
enum class backend_kind { streaming, arena };

/// How struct fields are dispatched on the wire.
///   by_name:     JSON, TOML — fields matched by string key.
///   by_position: bincode, avro — fields written/read sequentially.
///   by_tag:      protobuf, thrift — fields identified by numeric tag (future).
enum class field_mode { by_name, by_position, by_tag };

/// Detect whether a type declares the new-style backend_kind_v.
template <typename T>
concept has_backend_kind = requires { T::backend_kind_v; };

/// Detect whether a type declares field_mode_v.
template <typename T>
concept has_field_mode = requires { T::field_mode_v; };

}  // namespace kota::codec
