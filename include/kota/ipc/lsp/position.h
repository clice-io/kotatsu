#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "kota/ipc/lsp/protocol.h"

namespace kota::ipc::lsp {

/// Position unit encoding used by LSP line/character coordinates.
enum class PositionEncoding : std::uint8_t {
    /// Character counts UTF-8 code units (bytes).
    UTF8,

    /// Character counts UTF-16 code units.
    UTF16,

    /// Character counts UTF-32 code units (code points).
    UTF32,
};

/// Parses LSP encoding name (e.g. "utf-16") to `PositionEncoding`.
/// Unknown values fall back to `PositionEncoding::UTF16`.
PositionEncoding parse_position_encoding(std::string_view encoding);

/// Converts between byte offsets and LSP line/character positions for one text snapshot.
class PositionMapper {
public:
    /// Builds an index for `content` using the given position encoding.
    PositionMapper(std::string_view content, PositionEncoding encoding);

    /// Returns the zero-based line containing `offset`.
    std::uint32_t line_of(std::uint32_t offset) const;

    /// Returns the byte offset of the start of `line`.
    std::uint32_t line_start(std::uint32_t line) const;

    /// Returns the byte offset one past the line content (excluding '\n').
    std::uint32_t line_end_exclusive(std::uint32_t line) const;

    /// Converts a byte column on `line` to an LSP character column in current encoding.
    std::uint32_t character(std::uint32_t line, std::uint32_t byte_column) const;

    /// Measures the encoded character length of a byte range on `line`.
    std::uint32_t length(std::uint32_t line,
                         std::uint32_t begin_byte_column,
                         std::uint32_t end_byte_column) const;

    /// Converts a byte offset to LSP `Position{line, character}`.
    /// Returns `std::nullopt` when the offset is out of range.
    std::optional<protocol::Position> to_position(std::uint32_t offset) const;

    /// Converts LSP position to byte offset in the original text.
    /// Returns `std::nullopt` when the position is out of range.
    std::optional<std::uint32_t> to_offset(protocol::Position position) const;

    /// Measures `text` length in the current position encoding.
    std::uint32_t measure(std::string_view text) const;

private:
    std::string_view content;
    PositionEncoding encoding;
    std::vector<std::uint32_t> line_starts;
};

}  // namespace kota::ipc::lsp
