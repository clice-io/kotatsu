#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>

#include "kota/meta/type_info.h"
#include "kota/meta/type_kind.h"
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

/// Whether a T value is the document's root table itself rather than a value
/// boxed under boxed_root_key: judged on the representation the dispatch
/// resolves (annotations and toml-scoped reprs included), with kind_of's
/// test, where str-like or tuple-like wins over reflection. A raw Table is
/// its own root even though its range kind would box it.
template <typename T>
constexpr bool root_table_v = [] {
    using R = meta::resolved_repr_t<T, format>;
    constexpr auto kind = meta::kind_of<R>();
    return kind == meta::type_kind::structure || kind == meta::type_kind::map ||
           std::is_same_v<R, Table>;
}();

/// Whether T is a nullable root (an optional or pointer with no repr of its
/// own): absent, it is the empty document; present, the root is routed by
/// the value it wraps.
template <typename T>
constexpr bool nullable_root_v = std::is_same_v<meta::resolved_repr_t<T, format>, T> &&
                                 (meta::kind_of<T>() == meta::type_kind::optional ||
                                  meta::kind_of<T>() == meta::type_kind::pointer);

/// Where a toml++ node or parse error starts, when toml++ recorded it.
inline std::optional<rich_error::source_location> location_of(const ::toml::source_region& src) {
    if(src.begin.line == 0) {
        return std::nullopt;
    }
    return rich_error::source_location{
        .line = static_cast<std::size_t>(src.begin.line),
        .column = static_cast<std::size_t>(src.begin.column),
    };
}

}  // namespace detail

}  // namespace kota::codec::toml
