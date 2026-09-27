#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "codec/harness/visit/kit.h"
#include "kota/codec/fbs/fbs.h"

namespace kota::test {

/// The fbs backend for the protocol kit. Its documents are the finished
/// buffers to_bytes writes, read back through the verifying from_bytes.
struct Fbs {
    constexpr static std::string_view name = "fbs";
    /// Not self_describing: fields travel by slot in declaration order and a
    /// variant by its index; names and tags never reach the buffer.
    /// absent_fields: an empty nullable or a skipped field leaves its slot
    /// absent. nested_nulls: a nullable element is boxed in a one-slot
    /// table, and a map value sits in its entry's slot, either of which can
    /// be absent. non_finite: float cells hold NaN and the infinities as they
    /// are. A struct map key must inline (can_inline_struct_v): it is stored
    /// as its image and the entries sorted field by field. layout_computed:
    /// assert_config_layout_stable and apply_repr_impl reject what would
    /// shape the layout by value or config at compile time.
    constexpr static Caps caps{
        .absent_fields = true,
        .full_uint64 = true,
        .nested_nulls = true,
        .non_finite = true,
        .layout_computed = true,
        .untrusted_input = true,
        .format_tag = true,
    };
    using Encoded = std::vector<std::uint8_t>;

    template <typename Config = void, typename T>
    static auto encode(const T& value) {
        return codec::fbs::to_bytes<Config>(value);
    }

    template <typename Config = void, typename T>
    static auto decode(const Encoded& bytes, T& out) {
        return codec::fbs::from_bytes<Config>(bytes, out);
    }

    static std::string render(const Encoded& bytes) {
        return hex_dump(bytes);
    }
};

}  // namespace kota::test
