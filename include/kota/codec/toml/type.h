#pragma once

#include <string_view>

#include "kota/codec/visit/context.h"

// kotatsu's TOML backend converts every toml++ failure into std::expected /
// rich_error and never lets an exception escape, so toml++ is pinned to its
// no-exceptions mode unconditionally (independent of KOTA_ENABLE_EXCEPTIONS).
// This keeps exactly one parse/error code path across all build flavors and
// keeps EH out of TOML parsing entirely (see PR #168). All translation units
// must agree on this macro before including toml++, or the toml++ ABI
// namespaces (ex/noex) will not match across TUs.
#ifdef TOML_EXCEPTIONS
#if TOML_EXCEPTIONS
#error "kotatsu pins toml++ to TOML_EXCEPTIONS=0; do not predefine TOML_EXCEPTIONS=1"
#endif
#else
#define TOML_EXCEPTIONS 0
#endif

#if __has_include(<toml++/toml.hpp>)
#include "toml++/toml.hpp"
#else
#error "toml++/toml.hpp not found."
#endif

static_assert(!TOML_EXCEPTIONS, "toml++ must be in no-exceptions mode for kotatsu");

namespace kota::codec::toml {

/// Format tag: scopes a meta::repr specialization to the TOML backend
/// (meta::repr<T, codec::toml::format>).
///
/// # Lowerings
///
/// Human-readable text backend over toml++. TOML has no null and requires a
/// table at the root; both constraints drive the special cases below:
/// - root: values whose resolved repr is table-shaped (structure, map, raw
///   Table) become the root table; every other kind is boxed under the
///   `__value` key (detail::boxed_root_key); a null root is the empty
///   document. An engaged nullable root that serializes to an empty table
///   fails loudly — it would decode back as null.
/// - null: inside a table the key is omitted; inside an array it fails
///   (TOML arrays cannot hold null)
/// - integers → TOML integer (int64); a uint64 above int64::max fails
/// - float32/float64 → TOML float; nan_repr::Passthrough hands the raw
///   value to toml++ (TOML has nan/inf literals)
/// - character → single-codepoint string (the char's value 0-255 encoded as
///   UTF-8); decode accepts exactly one codepoint ≤ 255
/// - string → TOML string
/// - bytes → array of integer octets; decode range-checks each into [0, 255]
/// - enumeration → underlying integer, or the renamed name under
///   enum_repr::String
/// - array/set/tuple → TOML array
/// - map → table; non-string keys go through MapKeyWriter/MapKeyReader as
///   their decimal string form
/// - structure → table keyed by (renamed) field names
/// - variant → shaped by the spec's tag_mode (see encode_tagged_variant in
///   visit/encode.h); untagged variants emit the bare payload
struct format {};

using Table = ::toml::table;
using Array = ::toml::array;
using Node = ::toml::node;

using error = rich_error;

namespace detail {

constexpr inline std::string_view boxed_root_key = "__value";

}  // namespace detail

}  // namespace kota::codec::toml
