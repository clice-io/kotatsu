#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>

#include "kota/support/type_traits.h"
#include "kota/meta/repr.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/context.h"

#if __has_include(<flatbuffers/flatbuffers.h>)
#include "flatbuffers/flatbuffers.h"
#else
#error "flatbuffers/flatbuffers.h not found."
#endif

namespace kota::codec::fbs {

using builder_t = ::flatbuffers::FlatBufferBuilder;

using voffset_t = ::flatbuffers::voffset_t;
using uoffset_t = ::flatbuffers::uoffset_t;

using Table = ::flatbuffers::Table;
using String = ::flatbuffers::String;
template <typename T>
using Vector = ::flatbuffers::Vector<T>;

template <typename T>
using offset_t = ::flatbuffers::Offset<T>;
using table_offset_t = offset_t<Table>;
using string_offset_t = offset_t<String>;

using verifier_t = ::flatbuffers::Verifier;

/// Format tag: scopes a meta::repr specialization to the flatbuffers backend
/// (meta::repr<T, codec::fbs::format>).
///
/// # Lowerings
///
/// FlatBuffers binary built in two passes (allocate child offsets, then
/// write tables); layout_computed=true, so meta::dynamic reprs are rejected
/// at compile time. Field slots use voffsets first_field=4, field_step=2 in
/// declaration order; the buffer carries the "EVTO" identifier.
/// - root → always a table: structures map directly, tuples and variants
///   use their dedicated table layouts described below, any other kind is
///   boxed into a single-field table at the first slot, and a null root is
///   an empty table
/// - boolean → uint8 cell; character → int8 cell; long double → double
///   cell; other scalars → their natural fixed-width cells
/// - string → flatbuffers string; bytes → vector of uint8
/// - enumeration → underlying integer cell
/// - structure → table with one slot per field; structs satisfying
///   can_inline_struct_v (trivially copyable, standard-layout, unannotated
///   fields that are scalars other than long double, fixed-underlying-type
///   enums, or nested inline structs) may instead inline as fixed-size
///   structs inside vectors, with padding bytes encoded as zero and bool
///   bytes verified on decode
/// - array/set → vector; elements are laid out per element_layout (proxy.h):
///   scalar cells, strings, inline structs, tables (tuple-, variant-, and
///   other table-shaped elements use their own layouts), or boxed tables
///   for nullable elements and nested containers / byte blobs
/// - tuple → table with one slot per element
/// - map → sorted vector of two-field {key, value} entry tables; the
///   ordering key mirrors find_entry (strings lexicographic, enums by
///   underlying value, scalars by value) so lookups can binary-search
/// - variant → table with the u32 alternative index at the first slot and
///   the payload at the slot for that alternative (detail::field_slot);
///   a null alternative such as std::monostate leaves that slot absent
/// - optional/pointer → disengaged fields simply leave their slot absent;
///   as vector elements they degrade to boxed tables. A null payload leaves
///   the slot absent too, so an engaged optional<std::monostate> reads back
///   disengaged
struct format {};

/// Whether reflection sees a nonempty struct's fields: past the reflection
/// field limit meta::field_count() collapses to zero, indistinguishable from
/// a genuinely empty struct, so every field-walking lowering would treat the
/// struct as empty.
template <typename T>
constexpr bool fields_reflected_v = std::is_empty_v<T> || meta::field_count<T>() > 0;

namespace detail {

/// Static visitor traits shared by the fbs visitors the codec dispatch
/// drives: a binary backend whose output layout is computed statically (so
/// meta::dynamic reprs are rejected at compile time).
struct VisitorBase {
    using error_type = rich_error;
    using format = fbs::format;
    constexpr static bool human_readable = false;
    constexpr static bool layout_computed = true;
};

constexpr inline char buffer_identifier[] = "EVTO";
constexpr voffset_t first_field = 4;
constexpr voffset_t field_step = 2;

/// FlatBuffers computes every slot's layout statically from the type, so a
/// config knob that re-routes values to a different layout cannot apply.
/// Rejecting them here keeps the layout a pure function of the type — which
/// is also what lets the config-less zero-copy views and their verifier read
/// any buffer this backend produced.
template <typename Config>
consteval void assert_config_layout_stable() {
    static_assert(default_config<Config>::enum_repr == enum_repr::Integer,
                  "the fbs backend cannot honor enum_repr::String: it would change every enum "
                  "slot from an integer cell to a string offset; annotate individual fields "
                  "with behavior::enum_string instead");
    static_assert(default_config<Config>::nan_repr != nan_repr::String,
                  "the fbs backend cannot honor nan_repr::String: a float slot's shape would "
                  "depend on the value it holds");
}

/// Table lowerings walk a struct's reflected fields, so a nonempty struct
/// past the reflection field limit would encode as an empty table and decode
/// by discarding every input value — and it cannot inline either, because
/// the padding sanitization would zero the whole image. Every table path
/// asserts the shape away instead of silently dropping data.
template <typename T>
consteval void assert_fields_reflected() {
    static_assert(fields_reflected_v<T>,
                  "this struct has more fields than reflection supports; the fbs backend would "
                  "encode it as an empty table, silently discarding every field");
}

/// A verifier for an untrusted buffer. Depth stays at the flatbuffers
/// default (64) — it bounds the decode/verify recursion, so cyclic offsets
/// terminate — while the table-visit cap scales with the buffer: a table
/// occupies at least 4 bytes, so size/4 admits every legitimate buffer no
/// matter how large, yet aliased offsets cannot amplify verification work
/// past O(size). The +16 keeps sub-64-byte buffers from starving.
inline auto make_verifier(const std::uint8_t* data, std::size_t size) -> verifier_t {
    // Callers reject larger sizes first; this also keeps the cap within
    // uoffset_t.
    assert(size < FLATBUFFERS_MAX_BUFFER_SIZE);
    verifier_t::Options opts;
    opts.max_tables = static_cast<uoffset_t>(size / 4 + 16);
    return verifier_t(data, size, opts);
}

/// A buffer's root table and the verifier that checks everything under it.
struct OpenedBuffer {
    verifier_t verifier;
    const Table* root;
};

/// The checks every reader of a buffer starts with: room for the root
/// offset and the "EVTO" identifier (the smallest well-formed buffer),
/// a size below flatbuffers' maximum, the identifier, and an in-bounds
/// root offset. The root table itself is left for the caller to verify.
inline auto open_root(std::span<const std::byte> buf) -> std::expected<OpenedBuffer, rich_error> {
    if(buf.size() < 2 * sizeof(uoffset_t)) {
        return std::unexpected(rich_error("buffer too small"));
    }
    if(buf.size() >= FLATBUFFERS_MAX_BUFFER_SIZE) {
        return std::unexpected(rich_error("buffer too large"));
    }
    const auto* data = reinterpret_cast<const std::uint8_t*>(buf.data());
    if(!::flatbuffers::BufferHasIdentifier(data, buffer_identifier)) {
        return std::unexpected(rich_error("invalid buffer identifier"));
    }
    OpenedBuffer opened{.verifier = make_verifier(data, buf.size()), .root = nullptr};
    if(opened.verifier.VerifyOffset(0) == 0) {
        return std::unexpected(rich_error("buffer verification failed: root offset"));
    }
    opened.root = ::flatbuffers::GetRoot<Table>(data);
    return opened;
}

/// The vtable slot of a table's index-th field: a struct field, a tuple
/// element, a map entry's key (0) or value (1), or a variant's tag (0) and
/// then the payload of alternative i (i + 1). Indices come from a type's
/// field, element, and alternative counts, which assert_slots_fit bounds
/// where the type is known, so a slot never wraps; the one runtime-supplied
/// index, a decoded variant tag, is rejected by the dispatch before the
/// payload slot it names is read.
constexpr voffset_t field_slot(std::size_t index) {
    return static_cast<voffset_t>(first_field + field_step * index);
}

/// Fails to compile when a table with count fields would run past the last
/// voffset.
template <std::size_t Count>
consteval void assert_slots_fit() {
    constexpr std::size_t max_voffset = (std::numeric_limits<voffset_t>::max)();
    static_assert(Count == 0 || first_field + field_step * (Count - 1) <= max_voffset,
                  "the fbs backend gives every field, tuple element and variant alternative a "
                  "vtable slot; this type has more than a flatbuffers table can address");
}

}  // namespace detail

namespace detail {

/// A scalar whose native object representation is exactly its buffer cell.
/// long double is excluded: everywhere else in this backend it lowers to a
/// double cell (scalar_cell), because its native image is ABI-specific and
/// can carry internal padding (x86's 80-bit format leaves 6 of 16 bytes
/// unspecified — bytes a memcpy image would disclose). A struct holding one
/// stays table-shaped, where the field encodes as a deterministic double
/// cell.
template <typename T>
constexpr bool is_scalar_field_v =
    std::same_as<T, bool> || meta::int_like<T> || meta::uint_like<T> ||
    (meta::floating_like<T> && !std::same_as<T, long double>) || meta::char_like<T> ||
    std::same_as<T, std::byte>;

/// Whether an enum's underlying type is fixed (scoped, or declared with an
/// explicit base) — exactly the enums direct-list-initializable from an
/// integer. Only for these is every value of the underlying type a valid
/// value of the enum, the property a memcpy image needs: an enum with a
/// deduced base admits only the values of its minimal bit-field, so a
/// hostile image could hold a representation whose mere evaluation is
/// undefined behavior, invisible to size/alignment verification. Such a
/// field keeps the struct table-shaped, where the slot travels as a plain
/// integer cell.
template <typename T>
constexpr bool has_fixed_underlying_v = requires { T{0}; };

template <typename T>
consteval bool inline_struct_field();

/// can_inline_struct_v's test.
template <typename T>
consteval bool inline_struct() {
    // Trivially copyable is the exact bound the memcpy image needs; default
    // member initializers (which break std::is_trivial) are fine. Decode
    // restores an inline struct by whole-object assignment, which trivially
    // copyable alone does not promise: deleted assignment operators and
    // const members are admitted. Such a struct stays table-shaped, where
    // its fields decode individually.
    if constexpr(!meta::reflectable_class<T> || !std::is_trivially_copyable_v<T> ||
                 !std::is_copy_assignable_v<T> || !std::is_standard_layout_v<T>) {
        return false;
    } else if constexpr(!fields_reflected_v<T>) {
        // The padding sanitization would see no fields and zero the entire
        // image, so a struct whose fields reflection cannot see never joins a
        // memcpy image — and, recursing through inline_struct_field, neither
        // does any struct containing one. The table paths then reject it
        // outright (assert_fields_reflected).
        return false;
    } else {
        return []<std::size_t... I>(std::index_sequence<I...>) {
            return (inline_struct_field<meta::field_type<T, I>>() && ...);
        }(std::make_index_sequence<meta::field_count<T>()>{});
    }
}

/// Whether a field of type T can be part of an inline struct's image.
template <typename T>
consteval bool inline_struct_field() {
    // meta::field_type yields const-qualified types, so strip cv before
    // matching.
    using U = std::remove_cv_t<T>;
    if constexpr(meta::annotated_type<U>) {
        // An annotation's attrs replace the field's raw handling (with/as
        // substitute the shape, skip_if drops the slot), so an annotated
        // field cannot join a memcpy image at any nesting depth; the
        // enclosing struct degrades to a table, where the dispatch applies
        // the attrs. Checked before the reflection branch: an annotation
        // wrapping an aggregate is itself an aggregate that reflection
        // would walk right through, hiding the attrs.
        return false;
    } else if constexpr(is_optional_v<U>) {
        // std::optional<int> is trivially copyable, yet not every byte image
        // is a valid optional: the engagement flag admits only two values,
        // and inline structs are verified for size/alignment alone. An
        // optional field keeps the enclosing struct table-shaped, where
        // disengagement is an absent slot instead of a raw flag byte.
        return false;
    } else if constexpr(meta::has_repr<U, format>) {
        // A repr replaces the raw layout, so the field cannot be part of an
        // inline struct's memcpy image; the enclosing type degrades to a
        // table, where the dispatch applies the repr.
        return false;
    } else if constexpr(std::is_enum_v<U>) {
        return has_fixed_underlying_v<U>;
    } else if constexpr(is_scalar_field_v<U>) {
        return true;
    } else {
        return inline_struct<U>();
    }
}

}  // namespace detail

/// Whether a struct lowers to an inline fixed-size FlatBuffers struct — a
/// verbatim memcpy image — instead of a table: trivially copyable,
/// assignable, standard-layout, and every field (recursively) an
/// unannotated scalar, fixed-underlying-type enum, or such a struct.
template <typename T>
constexpr bool can_inline_struct_v = detail::inline_struct<T>();

}  // namespace kota::codec::fbs
