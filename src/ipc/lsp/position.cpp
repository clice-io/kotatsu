#include "kota/ipc/lsp/position.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <utility>

namespace {

// Decodes one UTF-8 code point starting at `index`.
// Returns:
// - first: consumed UTF-8 byte count
// - second: corresponding UTF-16 code unit count
// For invalid/truncated sequences it falls back to {1, 1} so callers can
// keep scanning forward without getting stuck.
std::pair<std::uint32_t, std::uint32_t> next_codepoint_sizes(std::string_view text,
                                                             std::size_t index) {
    assert(index < text.size() && "index out of range");

    // First byte >= ascii_limit starts a multi-byte UTF-8 sequence.
    constexpr unsigned char ascii_limit = 0x80u;

    // Continuation byte shape is 10xxxxxx.
    constexpr unsigned char continuation_mask = 0xC0u;
    constexpr unsigned char continuation_value = 0x80u;

    // Minimum valid 2-byte lead is C2 (C0/C1 are overlong).
    constexpr unsigned char two_byte_min = 0xC2u;

    // Lead-byte region boundaries.
    constexpr unsigned char three_byte_min = 0xE0u;
    constexpr unsigned char four_byte_min = 0xF0u;
    constexpr unsigned char four_byte_exclusive_max = 0xF5u;

    // E0 xx needs b2 >= A0 to avoid overlong 3-byte encoding.
    constexpr unsigned char three_byte_overlong_lead = 0xE0u;
    constexpr unsigned char three_byte_overlong_b2_min = 0xA0u;

    // ED A0..BF would encode UTF-16 surrogate range.
    constexpr unsigned char surrogate_lead = 0xEDu;
    constexpr unsigned char surrogate_b2_min = 0xA0u;

    // F0 xx needs b2 >= 90 to avoid overlong 4-byte encoding.
    constexpr unsigned char four_byte_overlong_lead = 0xF0u;
    constexpr unsigned char four_byte_overlong_b2_min = 0x90u;

    // F4 b2 must stay below 90 to remain <= U+10FFFF.
    constexpr unsigned char unicode_max_lead = 0xF4u;
    constexpr unsigned char unicode_max_b2_exclusive = 0x90u;

    // Inspect the leading byte to determine UTF-8 sequence width.
    const auto lead = static_cast<unsigned char>(text[index]);

    // 0xxxxxxx: ASCII, one UTF-8 byte and one UTF-16 code unit.
    if(lead < ascii_limit) [[likely]] {
        // ASCII is already a complete code point.
        return {1, 1};
    }

    // Invalid leading byte:
    // - 10xxxxxx: continuation byte
    // - 0xC0/0xC1: overlong 2-byte lead
    if(lead < two_byte_min) [[unlikely]] {
        // Invalid lead byte, consume one byte to keep forward progress.
        return {1, 1};
    }

    if(lead < three_byte_min) {
        // 2-byte UTF-8 lead range: C2..DF
        if(index + 2 > text.size()) [[unlikely]] {
            // Truncated 2-byte sequence at input end.
            return {1, 1};
        }

        // UTF-8 continuation byte must match 10xxxxxx.
        const auto b2 = static_cast<unsigned char>(text[index + 1]);
        if((b2 & continuation_mask) != continuation_value) [[unlikely]] {
            // Second byte is not a valid continuation byte.
            return {1, 1};
        }

        return {2, 1};
    }

    if(lead < four_byte_min) {
        // 3-byte UTF-8 lead range: E0..EF
        if(index + 3 > text.size()) [[unlikely]] {
            // Truncated 3-byte sequence at input end.
            return {1, 1};
        }

        const auto b2 = static_cast<unsigned char>(text[index + 1]);
        const auto b3 = static_cast<unsigned char>(text[index + 2]);
        if((b2 & continuation_mask) != continuation_value ||
           (b3 & continuation_mask) != continuation_value) [[unlikely]] {
            // One of the continuation bytes is malformed.
            return {1, 1};
        }

        // E0 A0..BF ...: prevent overlong 3-byte sequences.
        if(lead == three_byte_overlong_lead && b2 < three_byte_overlong_b2_min) [[unlikely]] {
            // Overlong encoding for a code point that needs fewer bytes.
            return {1, 1};
        }

        // ED 80..9F ...: exclude UTF-16 surrogate code points.
        if(lead == surrogate_lead && b2 >= surrogate_b2_min) [[unlikely]] {
            // UTF-16 surrogate range is invalid in UTF-8.
            return {1, 1};
        }

        return {3, 1};
    }

    if(lead < four_byte_exclusive_max) {
        // 4-byte UTF-8 lead range: F0..F4 (Unicode max U+10FFFF).
        if(index + 4 > text.size()) [[unlikely]] {
            // Truncated 4-byte sequence at input end.
            return {1, 1};
        }

        const auto b2 = static_cast<unsigned char>(text[index + 1]);
        const auto b3 = static_cast<unsigned char>(text[index + 2]);
        const auto b4 = static_cast<unsigned char>(text[index + 3]);
        if((b2 & continuation_mask) != continuation_value ||
           (b3 & continuation_mask) != continuation_value ||
           (b4 & continuation_mask) != continuation_value) [[unlikely]] {
            // One of the continuation bytes is malformed.
            return {1, 1};
        }

        // F0 90..BF ...: prevent overlong 4-byte sequences.
        if(lead == four_byte_overlong_lead && b2 < four_byte_overlong_b2_min) [[unlikely]] {
            // Overlong encoding for code points below U+10000.
            return {1, 1};
        }

        // F4 80..8F ...: stay within U+10FFFF upper bound.
        if(lead == unicode_max_lead && b2 >= unicode_max_b2_exclusive) [[unlikely]] {
            // Would decode beyond maximum Unicode scalar value.
            return {1, 1};
        }

        return {4, 2};
    }

    // F5..FF are invalid in UTF-8.
    // Invalid lead byte range, consume one byte conservatively.
    return {1, 1};
}

}  // namespace

namespace kota::ipc::lsp {

PositionEncoding parse_position_encoding(std::string_view encoding) {
    if(encoding == protocol::PositionEncodingKind::utf8) {
        return PositionEncoding::UTF8;
    }

    if(encoding == protocol::PositionEncodingKind::utf32) {
        return PositionEncoding::UTF32;
    }

    return PositionEncoding::UTF16;
}

PositionMapper::PositionMapper(std::string_view content, PositionEncoding encoding) :
    content(content), encoding(encoding) {
    line_starts.push_back(0);
    for(std::uint32_t i = 0; i < content.size(); ++i) {
        if(content[i] == '\n') {
            line_starts.push_back(i + 1);
        }
    }
}

std::uint32_t PositionMapper::line_of(std::uint32_t offset) const {
    assert(offset <= content.size() && "offset out of range");
    auto it = std::upper_bound(line_starts.begin(), line_starts.end(), offset);
    if(it == line_starts.begin()) {
        return 0;
    }
    return static_cast<std::uint32_t>((it - line_starts.begin()) - 1);
}

std::uint32_t PositionMapper::line_start(std::uint32_t line) const {
    assert(line < line_starts.size() && "line out of range");
    return line_starts[line];
}

std::uint32_t PositionMapper::line_end_exclusive(std::uint32_t line) const {
    assert(line < line_starts.size() && "line out of range");
    if(line + 1 < line_starts.size()) {
        return line_starts[line + 1] - 1;
    }
    return static_cast<std::uint32_t>(content.size());
}

std::uint32_t PositionMapper::measure(std::string_view text) const {
    if(encoding == PositionEncoding::UTF8) {
        return static_cast<std::uint32_t>(text.size());
    }

    std::uint32_t units = 0;
    for(std::size_t index = 0; index < text.size();) {
        auto [utf8, utf16] = next_codepoint_sizes(text, index);
        index += utf8;
        units += (encoding == PositionEncoding::UTF16) ? utf16 : 1;
    }
    return units;
}

std::uint32_t PositionMapper::character(std::uint32_t line, std::uint32_t byte_column) const {
    auto start = line_start(line);
    auto end = line_end_exclusive(line);
    assert(start + byte_column <= end && "byte column out of range");
    return measure(content.substr(start, byte_column));
}

std::uint32_t PositionMapper::length(std::uint32_t line,
                                     std::uint32_t begin_byte_column,
                                     std::uint32_t end_byte_column) const {
    auto start = line_start(line);
    auto end = line_end_exclusive(line);
    assert(start + begin_byte_column <= end && "begin byte column out of range");
    assert(start + end_byte_column <= end && "end byte column out of range");

    if(end_byte_column <= begin_byte_column) {
        return 0;
    }

    auto size = end_byte_column - begin_byte_column;
    return measure(content.substr(start + begin_byte_column, size));
}

std::optional<protocol::Position> PositionMapper::to_position(std::uint32_t offset) const {
    if(offset > content.size()) [[unlikely]] {
        return std::nullopt;
    }
    auto line = line_of(offset);
    auto column = offset - line_start(line);
    if(line_start(line) + column > line_end_exclusive(line)) [[unlikely]] {
        return std::nullopt;
    }
    return protocol::Position{
        .line = line,
        .character = character(line, column),
    };
}

std::optional<std::uint32_t> PositionMapper::to_offset(protocol::Position position) const {
    auto line = position.line;
    auto target = position.character;

    if(line >= line_starts.size()) [[unlikely]] {
        return std::nullopt;
    }

    auto begin = line_start(line);
    auto end = line_end_exclusive(line);

    if(target == 0) {
        return begin;
    }

    if(encoding == PositionEncoding::UTF8) {
        if(begin + target > end) [[unlikely]] {
            return std::nullopt;
        }
        return begin + target;
    }

    std::uint32_t offset = begin;
    auto text = content.substr(begin, end - begin);
    for(std::size_t index = 0; index < text.size();) {
        auto [utf8, utf16] = next_codepoint_sizes(text, index);
        auto step = (encoding == PositionEncoding::UTF16) ? utf16 : 1;
        if(target < step) [[unlikely]] {
            return std::nullopt;
        }
        target -= step;
        offset += utf8;
        index += utf8;
        if(target == 0) {
            return offset;
        }
    }

    return std::nullopt;
}

}  // namespace kota::ipc::lsp
