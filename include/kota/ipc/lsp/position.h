#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "kota/ipc/lsp/protocol.h"
#include "kota/ipc/lsp/text.h"

namespace kota::ipc::lsp {

/// Source content + line starts for LSP position conversion.
/// Line starts are held either as a borrowed span or as an owned vector.
///
/// A line ends at '\n', and a '\r' just before it belongs to the line's end,
/// not its text. A lone '\r' is text: line starts stay those
/// build_line_starts has always produced, which callers persist.
///
/// Lines without a byte past ASCII convert between bytes and every
/// encoding's units one to one, without reading the line.
class LineMap {
public:
    using Offset = std::uint32_t;

    struct LineBounds {
        /// Zero-based line number.
        Offset line;

        /// Byte offset of the line start.
        Offset start;

        /// Byte offset of the line end: its "\n" or "\r\n", or the end of
        /// the content.
        Offset end;
    };

    /// Compute line starts from content.
    explicit LineMap(std::string_view content, PositionEncoding encoding = PositionEncoding::UTF16);

    /// Borrow pre-computed line starts. Caller must keep the data alive.
    LineMap(std::string_view content,
            std::span<const Offset> line_starts,
            PositionEncoding encoding = PositionEncoding::UTF16);

    /// Take ownership of pre-computed line starts.
    LineMap(std::string_view content,
            std::vector<Offset>&& line_starts,
            PositionEncoding encoding = PositionEncoding::UTF16);

    /// Convert a byte offset to an LSP Position. Every offset up to the
    /// content's size has one: an offset inside a line's end is at the end.
    std::optional<protocol::Position>
        to_position(Offset offset, PositionEncoding encoding = PositionEncoding::Default) const;

    /// Convert an LSP Position to a byte offset. A character past the line's
    /// end is its end, as LSP asks; a line past the last one has no offset,
    /// nor has a UTF-16 unit inside a surrogate pair.
    std::optional<Offset> to_offset(protocol::Position position,
                                    PositionEncoding encoding = PositionEncoding::Default) const;

    /// Convert a byte range to an LSP Range.
    std::optional<protocol::Range>
        to_range(Offset begin,
                 Offset end,
                 PositionEncoding encoding = PositionEncoding::Default) const;

    /// Get line number and byte boundaries for the line containing the offset.
    LineBounds line_bounds(Offset offset) const;

    std::string_view content() const;

    std::span<const Offset> line_starts() const;

private:
    PositionEncoding resolve(PositionEncoding encoding) const;

    /// Where `line`'s text ends.
    Offset line_end(Offset line) const;

    /// `line` holds only ASCII.
    bool is_ascii(Offset line) const;

    std::string_view source;
    std::variant<std::vector<Offset>, std::span<const Offset>> starts;
    /// A bit for each line holding a byte past ASCII, from the first line's
    /// lowest bit on.
    std::vector<std::uint64_t> non_ascii_lines;
    PositionEncoding enc;
};

}  // namespace kota::ipc::lsp
