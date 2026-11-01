#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "codec/harness/visit/kit.h"
#include "kotatsu/codec/fbs/fbs.h"

namespace kotatsu::test {

/// The fbs backend for the protocol kit. Its documents are the finished
/// buffers to_bytes writes, read back through the verifying from_bytes.
struct Fbs {
    constexpr static std::string_view name = "fbs";
    /// Not self_describing: fields travel by slot in declaration order and a
    /// variant by its index; names and tags never reach the buffer.
    /// Not absent_fields: an absent slot is how a null travels, and reads as
    /// null, empty or zero, so every field is written, whatever its presence
    /// (VisitorBase::writes_every_field). nested_nulls: a nullable element is boxed in a
    /// one-slot table, and a map value sits in its entry's slot, either of
    /// which can be absent. non_finite: float cells hold NaN and the
    /// infinities as they are. A struct map key must inline
    /// (can_inline_struct_v): it is stored as its image and the entries
    /// sorted field by field. layout_computed: assert_config_layout_stable
    /// and apply_repr_impl reject what would shape the layout by value or
    /// config at compile time.
    constexpr static Caps caps{
        .full_uint64 = true,
        .nested_nulls = true,
        .non_finite = true,
        .layout_computed = true,
        .untrusted_input = true,
        .format_tag = true,
    };
    using Encoded = std::vector<std::uint8_t>;

    /// The settings are ignored: fbs takes no format, its layout being the
    /// type's alone, and no option bears on it.
    template <typename Config = void, typename T>
    static auto encode(const T& value) {
        return codec::fbs::to_bytes(value);
    }

    template <typename Config = void, typename T>
    static auto decode(const Encoded& bytes, T& out) {
        return codec::fbs::from_bytes(bytes, out);
    }

    static std::string render(const Encoded& bytes) {
        return hex_dump(bytes);
    }
};

}  // namespace kotatsu::test
