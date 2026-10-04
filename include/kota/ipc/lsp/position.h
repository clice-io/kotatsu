#pragma once

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>

#include "kota/ipc/lsp/range.h"
#include "kota/ipc/lsp/text.h"

// Conversions between byte offsets and LSP positions, in three forms:
//
// - over a text and its line starts, which the caller keeps: lines the
//   caller knows to hold only ASCII are not read;
// - over a text alone, finding its line starts for one conversion;
// - over a text known only by its size and line starts, every line ASCII,
//   such as an index that keeps no text: the caller says which lines end in
//   "\r\n", asked only of a line with text and a line after it, and the
//   encodings all count bytes.
//
// A line ends at '\n', and a '\r' just before it belongs to the line's end,
// not its text. A lone '\r' is text: line starts stay those line_starts()
// gives, which callers persist.

namespace kota::ipc::lsp {

/// A byte range of a text, from `begin` up to `end`.
struct OffsetRange {
    std::uint32_t begin;
    std::uint32_t end;
};

/// The byte offset each line of a text starts at, the first 0, ascending: the
/// vector line_starts() gives, or any random-access range of unsigned
/// integers a caller keeps them in.
template <typename Lines>
concept line_table =
    std::ranges::random_access_range<const Lines> && std::ranges::sized_range<const Lines> &&
    std::unsigned_integral<std::ranges::range_value_t<const Lines>>;

/// What a caller knows of which lines hold only ASCII, which convert between
/// bytes and every encoding's units one to one without being read: a
/// bool(std::uint32_t line) predicate, true for a line it knows to hold only
/// ASCII, or the bitmap non_ascii_lines() gives. A line said to hold only
/// ASCII that does not converts as if it did.
template <typename ASCII>
concept ascii_lines = std::predicate<const ASCII&, std::uint32_t> ||
                      std::convertible_to<const ASCII&, std::span<const std::uint64_t>>;

/// Every line holds only ASCII.
constexpr inline auto all_ascii = [](std::uint32_t) {
    return true;
};

/// The line holding byte `offset`, its line end included: the last line
/// starting at or before it.
template <line_table Lines>
std::uint32_t line_of(const Lines& lines, std::uint32_t offset) {
    assert(!std::ranges::empty(lines) && *std::ranges::begin(lines) == 0 &&
           "line starts begin with the first line's, 0");
    return static_cast<std::uint32_t>(std::ranges::upper_bound(lines, offset) -
                                      std::ranges::begin(lines) - 1);
}

namespace detail {

/// What the conversions know when the caller says nothing: no line is known
/// to hold only ASCII, so every line converted is read.
constexpr inline auto unknown_ascii = [](std::uint32_t) {
    return false;
};

/// One line of a text: its number, where it starts, where its text ends (at
/// its "\n" or "\r\n", or at the end of the text), and whether it is known to
/// hold only ASCII, which spares reading it.
struct Line {
    std::uint32_t number;
    std::uint32_t start;
    std::uint32_t end;
    bool ascii;
};

/// The position of byte `offset` of `line`, an offset past the line's text
/// being at its end and one inside a code point at the code point's start.
protocol::Position position_in(std::string_view content,
                               Line line,
                               std::uint32_t offset,
                               PositionEncoding encoding);

/// Where unit `character` of `line` falls, as locate() places it, a character
/// past the line's end being exactly at the end, as LSP asks.
Located offset_in(std::string_view content,
                  Line line,
                  std::uint32_t character,
                  PositionEncoding encoding);

template <ascii_lines ASCII>
bool known_ascii(const ASCII& ascii, std::uint32_t line) {
    if constexpr(std::predicate<const ASCII&, std::uint32_t>) {
        return ascii(line);
    } else {
        std::span<const std::uint64_t> bits = ascii;
        return line / 64 >= bits.size() || ((bits[line / 64] >> (line % 64)) & 1) == 0;
    }
}

/// Line `number` of a text of `size` bytes; `crlf(number, newline)` says
/// whether a '\r' precedes the '\n' at `newline` that ends it.
template <line_table Lines, ascii_lines ASCII, typename CRLF>
Line line_at(const Lines& lines,
             std::uint32_t number,
             std::uint32_t size,
             const ASCII& ascii,
             CRLF crlf) {
    auto starts = std::ranges::begin(lines);
    Line line{
        .number = number,
        .start = static_cast<std::uint32_t>(starts[number]),
        .end = size,
        .ascii = detail::known_ascii(ascii, number),
    };
    if(number + 1 < std::ranges::size(lines)) {
        line.end = static_cast<std::uint32_t>(starts[number + 1]) - 1;
        if(line.end > line.start && crlf(number, line.end)) {
            --line.end;
        }
    }
    return line;
}

/// What says whether a line of `content` ends in "\r\n": its bytes.
inline auto crlf_read(std::string_view content) {
    return [content](std::uint32_t, std::uint32_t newline) {
        return content[newline - 1] == '\r';
    };
}

/// What says whether a line of a text kept as its size ends in "\r\n": the
/// caller's `crlf`.
template <typename CRLF>
auto crlf_told(const CRLF& crlf) {
    return [&crlf](std::uint32_t line, std::uint32_t) {
        return crlf(line);
    };
}

template <line_table Lines, ascii_lines ASCII, typename CRLF>
std::optional<protocol::Position> position_of(std::string_view content,
                                              std::uint32_t size,
                                              const Lines& lines,
                                              std::uint32_t offset,
                                              PositionEncoding encoding,
                                              const ASCII& ascii,
                                              CRLF crlf) {
    if(offset > size) [[unlikely]] {
        return std::nullopt;
    }
    auto line = detail::line_at(lines, lsp::line_of(lines, offset), size, ascii, crlf);
    return detail::position_in(content, line, offset, encoding);
}

template <line_table Lines, ascii_lines ASCII, typename CRLF>
Located offset_of(std::string_view content,
                  std::uint32_t size,
                  const Lines& lines,
                  protocol::Position position,
                  PositionEncoding encoding,
                  const ASCII& ascii,
                  CRLF crlf) {
    if(position.line >= std::ranges::size(lines)) [[unlikely]] {
        return {.offset = size, .exact = false};
    }
    auto line = detail::line_at(lines, position.line, size, ascii, crlf);
    return detail::offset_in(content, line, position.character, encoding);
}

template <line_table Lines, ascii_lines ASCII, typename CRLF>
std::optional<protocol::Range> range_of(std::string_view content,
                                        std::uint32_t size,
                                        const Lines& lines,
                                        std::uint32_t begin,
                                        std::uint32_t end,
                                        PositionEncoding encoding,
                                        const ASCII& ascii,
                                        CRLF crlf) {
    if(begin > end) [[unlikely]] {
        return std::nullopt;
    }
    // The start is in the text if the end is.
    auto stop = detail::position_of(content, size, lines, end, encoding, ascii, crlf);
    if(!stop) {
        return std::nullopt;
    }
    return protocol::Range{
        .start = *detail::position_of(content, size, lines, begin, encoding, ascii, crlf),
        .end = *stop,
    };
}

template <line_table Lines, ascii_lines ASCII, typename CRLF>
OffsetRange offset_range_of(std::string_view content,
                            std::uint32_t size,
                            const Lines& lines,
                            protocol::Range range,
                            PositionEncoding encoding,
                            const ASCII& ascii,
                            CRLF crlf) {
    auto begin = detail::offset_of(content, size, lines, range.start, encoding, ascii, crlf).offset;
    auto end = detail::offset_of(content, size, lines, range.end, encoding, ascii, crlf).offset;
    // Clamping keeps the order of positions, so this is their order too.
    return {.begin = std::min(begin, end), .end = std::max(begin, end)};
}

}  // namespace detail

/// Convert a byte offset of `content` to a position. Every offset up to the
/// content's size has one: an offset inside a line's end is at the end, and
/// one inside a code point at the code point's start.
template <line_table Lines, ascii_lines ASCII = decltype(detail::unknown_ascii)>
std::optional<protocol::Position> to_position(std::string_view content,
                                              const Lines& lines,
                                              std::uint32_t offset,
                                              PositionEncoding encoding,
                                              const ASCII& ascii = {}) {
    return detail::position_of(content,
                               static_cast<std::uint32_t>(content.size()),
                               lines,
                               offset,
                               encoding,
                               ascii,
                               detail::crlf_read(content));
}

/// Convert a position to a byte offset of `content`. A character past the
/// line's end is its end, as LSP asks; a line past the last one has no
/// offset, nor has a unit inside a code point: a UTF-16 unit inside a
/// surrogate pair, or a UTF-8 byte inside a multi-byte sequence.
template <line_table Lines, ascii_lines ASCII = decltype(detail::unknown_ascii)>
std::optional<std::uint32_t> to_offset(std::string_view content,
                                       const Lines& lines,
                                       protocol::Position position,
                                       PositionEncoding encoding,
                                       const ASCII& ascii = {}) {
    auto found = detail::offset_of(content,
                                   static_cast<std::uint32_t>(content.size()),
                                   lines,
                                   position,
                                   encoding,
                                   ascii,
                                   detail::crlf_read(content));
    if(!found.exact) {
        return std::nullopt;
    }
    return found.offset;
}

/// Convert a position to a byte offset of `content`, giving every position
/// one: a line past the last one is the end of the content, as VS Code
/// answers; a character past the line's end is its end, as LSP asks; and a
/// unit inside a code point is the code point's start.
template <line_table Lines, ascii_lines ASCII = decltype(detail::unknown_ascii)>
std::uint32_t to_offset_clamped(std::string_view content,
                                const Lines& lines,
                                protocol::Position position,
                                PositionEncoding encoding,
                                const ASCII& ascii = {}) {
    return detail::offset_of(content,
                             static_cast<std::uint32_t>(content.size()),
                             lines,
                             position,
                             encoding,
                             ascii,
                             detail::crlf_read(content))
        .offset;
}

/// Convert the bytes of `content` from `begin` up to `end` to a range, as
/// to_position() converts each end; none when `begin` is past `end`.
template <line_table Lines, ascii_lines ASCII = decltype(detail::unknown_ascii)>
std::optional<protocol::Range> to_range(std::string_view content,
                                        const Lines& lines,
                                        std::uint32_t begin,
                                        std::uint32_t end,
                                        PositionEncoding encoding,
                                        const ASCII& ascii = {}) {
    return detail::range_of(content,
                            static_cast<std::uint32_t>(content.size()),
                            lines,
                            begin,
                            end,
                            encoding,
                            ascii,
                            detail::crlf_read(content));
}

/// Convert a range to the bytes of `content` it covers, each end as
/// to_offset_clamped() converts it; a range whose start is past its end
/// covers the bytes between them, as vscode-languageserver-textdocument reads
/// it.
template <line_table Lines, ascii_lines ASCII = decltype(detail::unknown_ascii)>
OffsetRange to_offset_range(std::string_view content,
                            const Lines& lines,
                            protocol::Range range,
                            PositionEncoding encoding,
                            const ASCII& ascii = {}) {
    return detail::offset_range_of(content,
                                   static_cast<std::uint32_t>(content.size()),
                                   lines,
                                   range,
                                   encoding,
                                   ascii,
                                   detail::crlf_read(content));
}

/// to_position() for a text whose line starts the caller does not keep: it
/// finds them for this one conversion. Keep line_starts() for more.
inline std::optional<protocol::Position> to_position(std::string_view content,
                                                     std::uint32_t offset,
                                                     PositionEncoding encoding) {
    return to_position(content, line_starts(content), offset, encoding);
}

/// to_offset() for a text whose line starts the caller does not keep.
inline std::optional<std::uint32_t> to_offset(std::string_view content,
                                              protocol::Position position,
                                              PositionEncoding encoding) {
    return to_offset(content, line_starts(content), position, encoding);
}

/// to_offset_clamped() for a text whose line starts the caller does not keep.
inline std::uint32_t to_offset_clamped(std::string_view content,
                                       protocol::Position position,
                                       PositionEncoding encoding) {
    return to_offset_clamped(content, line_starts(content), position, encoding);
}

/// to_range() for a text whose line starts the caller does not keep.
inline std::optional<protocol::Range> to_range(std::string_view content,
                                               std::uint32_t begin,
                                               std::uint32_t end,
                                               PositionEncoding encoding) {
    return to_range(content, line_starts(content), begin, end, encoding);
}

/// to_offset_range() for a text whose line starts the caller does not keep.
inline OffsetRange to_offset_range(std::string_view content,
                                   protocol::Range range,
                                   PositionEncoding encoding) {
    return to_offset_range(content, line_starts(content), range, encoding);
}

/// to_position() for a text of `size` bytes known by its line starts alone,
/// every line ASCII, in any encoding; `crlf(line)` says whether a line ends
/// in "\r\n".
template <line_table Lines, typename CRLF>
    requires std::predicate<const CRLF&, std::uint32_t>
std::optional<protocol::Position>
    to_position(std::uint32_t size, const Lines& lines, std::uint32_t offset, const CRLF& crlf) {
    return detail::position_of({},
                               size,
                               lines,
                               offset,
                               PositionEncoding::UTF8,
                               all_ascii,
                               detail::crlf_told(crlf));
}

/// to_offset() for a text of `size` bytes known by its line starts alone.
template <line_table Lines, typename CRLF>
    requires std::predicate<const CRLF&, std::uint32_t>
std::optional<std::uint32_t> to_offset(std::uint32_t size,
                                       const Lines& lines,
                                       protocol::Position position,
                                       const CRLF& crlf) {
    auto found = detail::offset_of({},
                                   size,
                                   lines,
                                   position,
                                   PositionEncoding::UTF8,
                                   all_ascii,
                                   detail::crlf_told(crlf));
    if(!found.exact) {
        return std::nullopt;
    }
    return found.offset;
}

/// to_offset_clamped() for a text of `size` bytes known by its line starts
/// alone.
template <line_table Lines, typename CRLF>
    requires std::predicate<const CRLF&, std::uint32_t>
std::uint32_t to_offset_clamped(std::uint32_t size,
                                const Lines& lines,
                                protocol::Position position,
                                const CRLF& crlf) {
    return detail::offset_of({},
                             size,
                             lines,
                             position,
                             PositionEncoding::UTF8,
                             all_ascii,
                             detail::crlf_told(crlf))
        .offset;
}

/// to_range() for a text of `size` bytes known by its line starts alone.
template <line_table Lines, typename CRLF>
    requires std::predicate<const CRLF&, std::uint32_t>
std::optional<protocol::Range> to_range(std::uint32_t size,
                                        const Lines& lines,
                                        std::uint32_t begin,
                                        std::uint32_t end,
                                        const CRLF& crlf) {
    return detail::range_of({},
                            size,
                            lines,
                            begin,
                            end,
                            PositionEncoding::UTF8,
                            all_ascii,
                            detail::crlf_told(crlf));
}

/// to_offset_range() for a text of `size` bytes known by its line starts
/// alone.
template <line_table Lines, typename CRLF>
    requires std::predicate<const CRLF&, std::uint32_t>
OffsetRange to_offset_range(std::uint32_t size,
                            const Lines& lines,
                            protocol::Range range,
                            const CRLF& crlf) {
    return detail::offset_range_of({},
                                   size,
                                   lines,
                                   range,
                                   PositionEncoding::UTF8,
                                   all_ascii,
                                   detail::crlf_told(crlf));
}

}  // namespace kota::ipc::lsp
