#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

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

/// The byte offset each line of `content` starts at: 0, then one past each
/// '\n'. A lone '\r' ends no line.
std::vector<std::uint32_t> line_starts(std::string_view content);

/// Whether every byte of `text` is ASCII.
bool is_ascii(std::string_view text);

/// A bit for each line of `content`, split as line_starts() splits it, set
/// when the line holds a byte past ASCII: line `n` is bit `n % 64` of word
/// `n / 64`. The words end at the last line with a bit set, so a text that is
/// all ASCII has none.
std::vector<std::uint64_t> non_ascii_lines(std::string_view content);

/// Returns `text` length in the given position encoding.
std::uint32_t encoded_length(std::string_view text, PositionEncoding encoding);

/// Converts an encoded character offset back to a byte offset within `text`.
/// Returns `std::nullopt` if the character offset is past the end, or inside
/// a code point: a UTF-16 unit inside a surrogate pair, or a UTF-8 byte
/// inside a multi-byte sequence.
std::optional<std::uint32_t> encoded_offset(std::string_view text,
                                            std::uint32_t character,
                                            PositionEncoding encoding);

namespace detail {

/// Where a unit was placed: its offset, and whether that offset is exactly
/// the unit's, or the nearest one to a unit that has none.
struct Located {
    std::uint32_t offset;
    bool exact;
};

/// Where unit `character` of `text`, counted in `encoding`'s units, falls:
/// at the start of the code point it begins, exact, or lies inside, not
/// exact; at the text's end past that, exact only for the end itself.
Located locate(std::string_view text, std::uint32_t character, PositionEncoding encoding);

}  // namespace detail

}  // namespace kota::ipc::lsp
