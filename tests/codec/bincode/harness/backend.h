#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "codec/harness/visit/kit.h"
#include "kota/codec/bincode/bincode.h"

namespace kota::test {

/// The bincode backend for the protocol kit. Its documents are the bytes
/// to_bytes writes: values in declaration order, with no names, tags or
/// markers beyond length prefixes, presence bytes and variant indices.
struct Bincode {
    constexpr static std::string_view name = "bincode";
    /// Not self_describing: a document is read by position with the type
    /// that wrote it, so names, aliases and tags never reach it, and a
    /// tagged variant travels as its index. Not absent_fields: fields are
    /// concatenated with nothing to mark one missing, so skip_if writes
    /// every field (Writer::writes_every_field). non_finite: a float travels
    /// as its IEEE double bits. struct_keys: a map key is written with the
    /// value writer, so a struct key is its fields. dynamic_repr: a dynamic
    /// repr compiles and writes through the Writer; with no peek_kind to
    /// decide by, one that reads back must frame what it writes, as
    /// codec_bincode_decode pins.
    constexpr static Caps caps{
        .full_uint64 = true,
        .null_elements = true,
        .non_finite = true,
        .struct_keys = true,
        .string_knobs = true,
        .dynamic_repr = true,
        .untrusted_input = true,
        .format_tag = true,
    };
    using Encoded = std::vector<std::byte>;

    template <typename Config = void, typename T>
    static auto encode(const T& value) {
        return codec::bincode::to_bytes<Config>(value);
    }

    template <typename Config = void, typename T>
    static auto decode(const Encoded& bytes, T& out) {
        return codec::bincode::from_bytes<Config>(bytes, out);
    }

    /// Sixteen bytes a line in hex, after the offset of the first.
    static std::string render(const Encoded& bytes) {
        std::string text;
        for(std::size_t at = 0; at < bytes.size(); ++at) {
            if(at % 16 == 0) {
                text += std::format("{}{:04x}:", at == 0 ? "" : "\n", at);
            }
            text += std::format(" {:02x}", static_cast<unsigned>(bytes[at]));
        }
        return text;
    }
};

}  // namespace kota::test
