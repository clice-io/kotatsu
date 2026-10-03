#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace kota::ipc::lsp {

/// Position unit encoding used by LSP line/character coordinates.
enum class PositionEncoding : std::uint8_t {
    /// Sentinel: use the encoding stored in the context.
    Default,

    /// Character counts UTF-8 code units (bytes).
    UTF8,

    /// Character counts UTF-16 code units.
    UTF16,

    /// Character counts UTF-32 code units (code points).
    UTF32,
};

/// Scans `content` for newlines and returns byte offsets of each line start.
std::vector<std::uint32_t> build_line_starts(std::string_view content);

/// Returns `text` length in the given position encoding.
std::uint32_t encoded_length(std::string_view text, PositionEncoding encoding);

/// Converts an encoded character offset back to a byte offset within `text`.
/// Returns `std::nullopt` if the character offset is past the end, or inside
/// a code point: a UTF-16 unit inside a surrogate pair, or a UTF-8 byte
/// inside a multi-byte sequence.
std::optional<std::uint32_t> encoded_offset(std::string_view text,
                                            std::uint32_t character,
                                            PositionEncoding encoding);

}  // namespace kota::ipc::lsp
