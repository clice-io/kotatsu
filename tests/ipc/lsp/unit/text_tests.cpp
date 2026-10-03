#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/ipc/lsp/text.h"

namespace kota::ipc::lsp {

namespace {

constexpr PositionEncoding encodings[] = {
    PositionEncoding::UTF8,
    PositionEncoding::UTF16,
    PositionEncoding::UTF32,
};

/// Bytes, as text, for sequences that are not valid UTF-8.
template <typename... Bytes>
std::string bytes(Bytes... values) {
    return std::string{static_cast<char>(values)...};
}

/// `text` in hex, for reports.
std::string hex(std::string_view text) {
    std::string shown;
    for(char c: text) {
        shown += std::format("{:02x} ", static_cast<unsigned char>(c));
    }
    return shown;
}

/// Bytes that are not UTF-8, each of which counts as a code point of its
/// own, the scan going on from the next byte: a lead byte without its
/// continuation bytes, overlong forms, surrogates, code points past
/// U+10FFFF, truncated sequences and stray continuation bytes.
const std::string not_utf8[] = {
    bytes('a', 0xE4, 'X', 'b'),
    bytes(0xC2, 'A'),
    bytes(0xE1, 0x80, 'B'),
    bytes(0xF1, 'C', 0x80, 0x80),
    bytes(0xF1, 0x80, 0x80, 'D'),
    bytes(0xC0, 0x80),
    bytes(0xE0, 0x80, 0x80),
    bytes(0xED, 0xA0, 0x80),
    bytes(0xF4, 0x90, 0x80, 0x80),
    bytes(0xF5, 0x80, 0x80, 0x80),
    bytes('a', 0xF0, 0x9F, 'b'),
    bytes(0x80, 0xBF, 'a'),
};

// Throughout this suite: 你 is 3 UTF-8 bytes and one UTF-16 unit, 🙂 is 4
// UTF-8 bytes and two UTF-16 units.
ZEST_SUITE(ipc_lsp_text) {

ZEST_CASE(build_line_starts_marks_each_line) {
    EXPECT(build_line_starts("") == std::vector<std::uint32_t>{0});
    EXPECT(build_line_starts("ab\n\ncd\n") == std::vector<std::uint32_t>{0, 3, 4, 7});
}

ZEST_CASE(encoded_length_counts_units_of_the_encoding) {
    std::string_view content = "a你🙂z";

    EXPECT(encoded_length(content, PositionEncoding::UTF8) == 9U);
    EXPECT(encoded_length(content, PositionEncoding::UTF16) == 5U);
    EXPECT(encoded_length(content, PositionEncoding::UTF32) == 4U);
}

ZEST_CASE(encoded_length_counts_what_is_not_utf8_byte_by_byte) {
    for(const auto& text: not_utf8) {
        for(auto encoding: encodings) {
            ZEST_CONTEXT("text: {}, encoding: {}", hex(text), static_cast<int>(encoding));
            EXPECT(encoded_length(text, encoding) == text.size());
        }
    }
}

ZEST_CASE(encoded_offset_maps_units_back_to_bytes) {
    std::string_view content = "a你🙂b";

    EXPECT(encoded_offset(content, 0, PositionEncoding::UTF16) == 0U);
    EXPECT(encoded_offset(content, 2, PositionEncoding::UTF16) == 4U);
    EXPECT(encoded_offset(content, 4, PositionEncoding::UTF16) == 8U);
    EXPECT(encoded_offset(content, 5, PositionEncoding::UTF16) == 9U);
    EXPECT(encoded_offset(content, 3, PositionEncoding::UTF32) == 8U);
    EXPECT(encoded_offset(content, 4, PositionEncoding::UTF8) == 4U);
}

// UTF-16 unit 3 is the second half of 🙂's surrogate pair.
ZEST_CASE(encoded_offset_inside_a_code_point_fails) {
    EXPECT(encoded_offset("a你🙂b", 3, PositionEncoding::UTF16) == std::nullopt);
}

// Bytes 2 and 3 are inside 你, 5 to 7 inside 🙂.
ZEST_CASE(encoded_offset_inside_a_utf8_sequence_fails) {
    std::string_view content = "a你🙂b";

    for(std::uint32_t character: {2U, 3U, 5U, 6U, 7U}) {
        ZEST_CONTEXT("character {}", character);
        EXPECT(encoded_offset(content, character, PositionEncoding::UTF8) == std::nullopt);
    }
    for(std::uint32_t character: {0U, 1U, 4U, 8U, 9U}) {
        ZEST_CONTEXT("character {}", character);
        EXPECT(encoded_offset(content, character, PositionEncoding::UTF8) == character);
    }
}

// Every byte encoded_length counts as a code point of its own has an offset.
ZEST_CASE(encoded_offset_steps_through_what_is_not_utf8_byte_by_byte) {
    for(const auto& text: not_utf8) {
        for(auto encoding: encodings) {
            for(std::uint32_t character = 0; character <= text.size(); ++character) {
                ZEST_CONTEXT("text: {}, encoding: {}, character: {}",
                             hex(text),
                             static_cast<int>(encoding),
                             character);
                EXPECT(encoded_offset(text, character, encoding) == character);
            }
        }
    }
}

ZEST_CASE(encoded_offset_past_the_end_fails) {
    for(auto encoding: encodings) {
        ZEST_CONTEXT("encoding: {}", static_cast<int>(encoding));
        EXPECT(encoded_offset("abc", 4, encoding) == std::nullopt);
    }
}

};  // ZEST_SUITE(ipc_lsp_text)

}  // namespace

}  // namespace kota::ipc::lsp
