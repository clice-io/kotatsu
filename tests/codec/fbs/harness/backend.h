#pragma once

#include <cstdint>
#include <format>
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
    /// absent. null_elements: a nullable element is boxed in a one-slot
    /// table, and a map value sits in its entry's slot, either of which can
    /// be absent. non_finite: float cells hold NaN and the infinities as they
    /// are. struct_keys: an inline-struct key (can_inline_struct_v) is stored
    /// as its image and the entries sorted field by field. Not string_knobs
    /// and not dynamic_repr: the layout is a function of the type alone, so
    /// assert_config_layout_stable and apply_repr_impl reject them at compile
    /// time. builder_layout: flatbuffers' builder places tables, vtables and
    /// padding itself.
    constexpr static Caps caps{
        .absent_fields = true,
        .full_uint64 = true,
        .null_elements = true,
        .non_finite = true,
        .struct_keys = true,
        .untrusted_input = true,
        .format_tag = true,
        .builder_layout = true,
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

    /// Hex, sixteen bytes to a line.
    static std::string render(const Encoded& bytes) {
        std::string text;
        for(std::size_t i = 0; i < bytes.size(); ++i) {
            text += std::format("{:02x}{}", bytes[i], (i % 16 == 15) ? "\n" : " ");
        }
        return text;
    }
};

}  // namespace kota::test
