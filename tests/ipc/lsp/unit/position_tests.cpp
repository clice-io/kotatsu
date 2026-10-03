#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/ipc/lsp/position.h"

namespace kota::ipc::lsp {
namespace {

// Throughout this suite: 你 is 3 UTF-8 bytes, 🙂 is 4.
ZEST_SUITE(ipc_lsp_position) {

ZEST_CASE(utf16_column_counts) {
    std::string_view content = "a你b\n";
    LineMap map(content, PositionEncoding::UTF16);

    auto position = map.to_position(4);
    ASSERT(position);
    ASSERT(position->line == 0U);
    ASSERT(position->character == 2U);
}

ZEST_CASE(offsets_roundtrip) {
    std::string_view content = "a你b\nx🙂y";
    constexpr std::uint32_t offsets[] = {0, 1, 4, 5, 6, 7, 11, 12};

    for(auto encoding: {PositionEncoding::UTF8, PositionEncoding::UTF16, PositionEncoding::UTF32}) {
        LineMap map(content, encoding);
        for(auto offset: offsets) {
            auto position = map.to_position(offset);
            ASSERT(position);
            auto mapped = map.to_offset(*position);
            ASSERT(mapped);
            ASSERT(*mapped == offset);
        }
    }
}

ZEST_CASE(position_offset_values) {
    std::string_view content = "a你🙂b\nx";

    struct Sample {
        std::uint32_t offset;
        std::uint32_t line;
        std::uint32_t utf8_character;
        std::uint32_t utf16_character;
        std::uint32_t utf32_character;
    };

    constexpr Sample samples[] = {
        {.offset = 0,  .line = 0, .utf8_character = 0, .utf16_character = 0, .utf32_character = 0},
        {.offset = 1,  .line = 0, .utf8_character = 1, .utf16_character = 1, .utf32_character = 1},
        {.offset = 4,  .line = 0, .utf8_character = 4, .utf16_character = 2, .utf32_character = 2},
        {.offset = 8,  .line = 0, .utf8_character = 8, .utf16_character = 4, .utf32_character = 3},
        {.offset = 9,  .line = 0, .utf8_character = 9, .utf16_character = 5, .utf32_character = 4},
        {.offset = 10, .line = 1, .utf8_character = 0, .utf16_character = 0, .utf32_character = 0},
        {.offset = 11, .line = 1, .utf8_character = 1, .utf16_character = 1, .utf32_character = 1},
    };

    LineMap map8(content, PositionEncoding::UTF8);
    LineMap map16(content, PositionEncoding::UTF16);
    LineMap map32(content, PositionEncoding::UTF32);

    for(const auto& sample: samples) {
        auto p8 = map8.to_position(sample.offset);
        ASSERT(p8);
        EXPECT(p8->line == sample.line);
        EXPECT(p8->character == sample.utf8_character);
        auto o8 = map8.to_offset(*p8);
        ASSERT(o8);
        EXPECT(*o8 == sample.offset);

        auto p16 = map16.to_position(sample.offset);
        ASSERT(p16);
        EXPECT(p16->line == sample.line);
        EXPECT(p16->character == sample.utf16_character);
        auto o16 = map16.to_offset(*p16);
        ASSERT(o16);
        EXPECT(*o16 == sample.offset);

        auto p32 = map32.to_position(sample.offset);
        ASSERT(p32);
        EXPECT(p32->line == sample.line);
        EXPECT(p32->character == sample.utf32_character);
        auto o32 = map32.to_offset(*p32);
        ASSERT(o32);
        EXPECT(*o32 == sample.offset);
    }
}

ZEST_CASE(line_bounds_values) {
    std::string_view content = "ab\n\ncd";
    LineMap map(content);

    auto b0 = map.line_bounds(0);
    EXPECT(b0.line == 0U);
    EXPECT(b0.start == 0U);
    EXPECT(b0.end == 2U);

    auto b1 = map.line_bounds(3);
    EXPECT(b1.line == 1U);
    EXPECT(b1.start == 3U);
    EXPECT(b1.end == 3U);

    auto b2 = map.line_bounds(4);
    EXPECT(b2.line == 2U);
    EXPECT(b2.start == 4U);
    EXPECT(b2.end == 6U);

    EXPECT(map.line_bounds(2).line == 0U);
    EXPECT(map.line_bounds(6).line == 2U);
}

ZEST_CASE(multiline_boundaries_roundtrip) {
    std::string_view content = "a你\n🙂b";
    constexpr std::uint32_t boundaries[] = {0, 1, 4, 5, 9, 10};

    for(auto encoding: {PositionEncoding::UTF8, PositionEncoding::UTF16, PositionEncoding::UTF32}) {
        LineMap map(content, encoding);
        for(auto offset: boundaries) {
            auto position = map.to_position(offset);
            ASSERT(position);
            auto mapped = map.to_offset(*position);
            ASSERT(mapped);
            ASSERT(*mapped == offset);
        }
    }
}

ZEST_CASE(invalid_position_stability) {
    auto expect_stable = [&](std::string_view content) {
        for(auto encoding:
            {PositionEncoding::UTF8, PositionEncoding::UTF16, PositionEncoding::UTF32}) {
            LineMap map(content, encoding);
            for(std::uint32_t offset = 0; offset <= content.size(); ++offset) {
                auto position = map.to_position(offset);
                ASSERT(position);
                auto mapped_offset = map.to_offset(*position);
                ASSERT(mapped_offset);
                EXPECT(*mapped_offset <= content.size());
            }
        }
    };

    auto expect_stable_bytes = [&](auto... bytes) {
        const char raw[] = {static_cast<char>(bytes)...};
        expect_stable(std::string_view(raw, sizeof...(bytes)));
    };

    expect_stable_bytes('a', 0xE4u, 'X', 'b');
    expect_stable_bytes('x', 0xF0u, 0x9Fu, '\n', 'y');
    expect_stable_bytes(0xF5u, 0x80u, 0x80u, 0x80u, '\n', 'z');
}

ZEST_CASE(to_position_past_the_end_fails) {
    std::string_view content = "abc\ndef";
    LineMap map(content, PositionEncoding::UTF8);

    EXPECT(!map.to_position(100).has_value());
    EXPECT(!map.to_position(8).has_value());
    EXPECT(map.to_position(7).has_value());
}

ZEST_CASE(to_offset_past_the_last_line_fails) {
    std::string_view content = "abc\ndef";
    LineMap map(content, PositionEncoding::UTF8);

    EXPECT(!map.to_offset({.line = 5, .character = 0}).has_value());
    EXPECT(!map.to_offset({.line = 2, .character = 0}).has_value());
    EXPECT(map.to_offset({.line = 1, .character = 0}).has_value());
}

ZEST_CASE(to_offset_at_the_line_end_is_the_line_end) {
    std::string_view content = "abc\ndef";

    for(auto encoding: {PositionEncoding::UTF8, PositionEncoding::UTF16, PositionEncoding::UTF32}) {
        LineMap map(content, encoding);
        ZEST_CONTEXT("encoding: {}", static_cast<int>(encoding));
        EXPECT(map.to_offset({.line = 0, .character = 3}) == 3U);
        EXPECT(map.to_offset({.line = 1, .character = 3}) == 7U);
    }
}

// LSP 3.17: a character past the line's length defaults back to it.
ZEST_CASE(to_offset_past_the_line_end_clamps_to_it) {
    std::string_view content = "abc\ndef";

    for(auto encoding: {PositionEncoding::UTF8, PositionEncoding::UTF16, PositionEncoding::UTF32}) {
        LineMap map(content, encoding);
        ZEST_CONTEXT("encoding: {}", static_cast<int>(encoding));
        EXPECT(map.to_offset({.line = 0, .character = 10}) == 3U);
        EXPECT(map.to_offset({.line = 1, .character = 4}) == 7U);
    }
}

ZEST_CASE(crlf_ends_a_line) {
    std::string_view content = "ab\r\ncd";
    LineMap map(content, PositionEncoding::UTF16);

    EXPECT(map.line_bounds(0).end == 2U);
    auto inside = map.to_position(3);
    ASSERT(inside.has_value());
    EXPECT(*inside == protocol::Position{.line = 0, .character = 2});
    EXPECT(map.to_offset({.line = 0, .character = 3}) == 2U);
}

// Line starts stay what build_line_starts gives, which callers persist.
ZEST_CASE(lone_cr_is_text) {
    std::string_view content = "a\rb\nc";
    LineMap map(content, PositionEncoding::UTF16);

    EXPECT(std::vector(map.line_starts().begin(), map.line_starts().end()) ==
           std::vector<std::uint32_t>{0, 4});
    EXPECT(map.line_bounds(0).end == 3U);
    auto after_cr = map.to_position(2);
    ASSERT(after_cr.has_value());
    EXPECT(*after_cr == protocol::Position{.line = 0, .character = 2});
}

ZEST_CASE(to_offset_past_a_non_ascii_line_end_clamps_to_it) {
    std::string_view content = "a你b\r\nc";

    struct Sample {
        PositionEncoding encoding;
        /// The line's length in the encoding's units.
        std::uint32_t length;
    };

    for(auto sample: {
            Sample{PositionEncoding::UTF8,  5},
            Sample{PositionEncoding::UTF16, 3},
            Sample{PositionEncoding::UTF32, 3}
    }) {
        LineMap map(content, sample.encoding);
        ZEST_CONTEXT("encoding: {}", static_cast<int>(sample.encoding));
        EXPECT(map.to_offset({.line = 0, .character = sample.length}) == 5U);
        EXPECT(map.to_offset({.line = 0, .character = sample.length + 1}) == 5U);
        EXPECT(map.to_offset({.line = 0, .character = 99}) == 5U);
    }
}

// UTF-16 unit 1 of "🙂" is the second half of its surrogate pair.
ZEST_CASE(to_offset_inside_a_surrogate_pair_fails) {
    LineMap map("🙂", PositionEncoding::UTF16);

    EXPECT(map.to_offset({.line = 0, .character = 1}) == std::nullopt);
}

// Bytes 2 to 4 of "a🙂b" are inside 🙂, which starts at byte 1.
ZEST_CASE(to_offset_inside_a_utf8_sequence_fails) {
    LineMap map("a🙂b", PositionEncoding::UTF8);

    for(std::uint32_t character: {2U, 3U, 4U}) {
        ZEST_CONTEXT("character {}", character);
        EXPECT(map.to_offset({.line = 0, .character = character}) == std::nullopt);
    }
    EXPECT(map.to_offset({.line = 0, .character = 1}) == 1U);
    EXPECT(map.to_offset({.line = 0, .character = 5}) == 5U);
}

ZEST_CASE(to_position_inside_a_code_point_is_at_its_start) {
    std::string_view content = "a🙂b";

    for(auto encoding: {PositionEncoding::UTF8, PositionEncoding::UTF16, PositionEncoding::UTF32}) {
        LineMap map(content, encoding);
        for(std::uint32_t offset: {2U, 3U, 4U}) {
            ZEST_CONTEXT("encoding {}, offset {}", static_cast<int>(encoding), offset);
            auto position = map.to_position(offset);
            ASSERT(position.has_value());
            EXPECT(*position == protocol::Position{.line = 0, .character = 1});
        }
    }
}

// So a range never runs backwards, whatever bytes its ends fall on.
ZEST_CASE(to_position_never_decreases_with_the_offset) {
    std::string_view content = "a你b\r\n🙂x\n\xF0\x9F\x99y";

    for(auto encoding: {PositionEncoding::UTF8, PositionEncoding::UTF16, PositionEncoding::UTF32}) {
        LineMap map(content, encoding);
        protocol::Position last{};
        for(std::uint32_t offset = 0; offset <= content.size(); ++offset) {
            ZEST_CONTEXT("encoding {}, offset {}", static_cast<int>(encoding), offset);
            auto position = map.to_position(offset);
            ASSERT(position.has_value());
            EXPECT(position->line >= last.line);
            if(position->line == last.line) {
                EXPECT(position->character >= last.character);
            }
            last = *position;
        }
    }
}

ZEST_CASE(to_offset_clamped_past_the_last_line_is_the_end) {
    std::string_view content = "abc\ndef";
    LineMap map(content, PositionEncoding::UTF16);

    EXPECT(map.to_offset_clamped({.line = 2, .character = 0}) == 7U);
    EXPECT(map.to_offset_clamped({.line = 9, .character = 4}) == 7U);
}

ZEST_CASE(to_offset_clamped_inside_a_code_point_is_its_start) {
    std::string_view content = "a🙂b";

    LineMap utf16(content, PositionEncoding::UTF16);
    EXPECT(utf16.to_offset_clamped({.line = 0, .character = 2}) == 1U);

    LineMap utf8(content, PositionEncoding::UTF8);
    for(std::uint32_t character: {2U, 3U, 4U}) {
        ZEST_CONTEXT("character {}", character);
        EXPECT(utf8.to_offset_clamped({.line = 0, .character = character}) == 1U);
    }
}

// Where to_offset has an offset, to_offset_clamped has the same; elsewhere,
// one to_position and to_offset take back unchanged.
ZEST_CASE(to_offset_clamped_lands_on_a_place_in_the_text) {
    std::string_view content = "a你🙂b\r\nxy\n\n🙂";

    for(auto encoding: {PositionEncoding::UTF8, PositionEncoding::UTF16, PositionEncoding::UTF32}) {
        LineMap map(content, encoding);
        const auto lines = static_cast<std::uint32_t>(map.line_starts().size());
        for(std::uint32_t line = 0; line <= lines; ++line) {
            for(std::uint32_t character = 0; character < 14; ++character) {
                ZEST_CONTEXT("encoding {}, {}:{}", static_cast<int>(encoding), line, character);
                protocol::Position position{.line = line, .character = character};
                auto clamped = map.to_offset_clamped(position);
                if(auto offset = map.to_offset(position)) {
                    EXPECT(clamped == *offset);
                    continue;
                }
                auto back = map.to_position(clamped);
                ASSERT(back.has_value());
                EXPECT(map.to_offset(*back) == clamped);
            }
        }
    }
}

// ASCII lines take a shortcut other lines do not: every line, in every
// encoding, converts as a walk of its text would.
ZEST_CASE(lines_convert_as_their_text_whatever_their_neighbours) {
    std::string_view content = "abc\r\na你b\n\n🙂z\nxyz";
    auto starts = build_line_starts(content);
    for(auto encoding: {PositionEncoding::UTF8, PositionEncoding::UTF16, PositionEncoding::UTF32}) {
        LineMap owned(content, encoding);
        LineMap borrowed(content, std::span<const std::uint32_t>(starts), encoding);
        for(std::uint32_t offset = 0; offset <= content.size(); ++offset) {
            ZEST_CONTEXT("encoding {}, offset {}", static_cast<int>(encoding), offset);
            auto bounds = owned.line_bounds(offset);
            // An offset inside a code point counts to the code point's start;
            // the text is valid UTF-8, so that start is the nearest byte back
            // that is no continuation byte.
            auto cut = std::min(offset, bounds.end);
            while(cut > bounds.start && cut < bounds.end &&
                  (static_cast<unsigned char>(content[cut]) & 0xC0) == 0x80) {
                --cut;
            }
            auto text = content.substr(bounds.start, cut - bounds.start);
            protocol::Position expected{.line = bounds.line,
                                        .character = encoded_length(text, encoding)};
            auto position = owned.to_position(offset);
            ASSERT(position.has_value());
            EXPECT(*position == expected);
            auto from_borrowed = borrowed.to_position(offset);
            ASSERT(from_borrowed.has_value());
            EXPECT(*from_borrowed == expected);
        }
    }
}

// Which lines are ASCII is kept a bit per line, 64 to a word.
ZEST_CASE(non_ascii_lines_far_apart_convert_by_their_text) {
    std::string content;
    for(int line = 0; line < 200; ++line) {
        content += (line == 70 || line == 130) ? "a你b\n" : "abcde\n";
    }
    LineMap map(content, PositionEncoding::UTF16);

    for(std::uint32_t line: {69U, 70U, 71U, 129U, 130U, 199U}) {
        ZEST_CONTEXT("line {}", line);
        auto start = map.line_starts()[line];
        auto end = map.to_position(start + 4);
        ASSERT(end.has_value());
        const bool wide = line == 70 || line == 130;
        EXPECT(*end == protocol::Position{.line = line, .character = wide ? 2U : 4U});
        EXPECT(map.to_offset({.line = line, .character = 2}) == start + (wide ? 4U : 2U));
    }
}

ZEST_CASE(encoding_override) {
    std::string_view content = "a你b\n";
    LineMap map(content, PositionEncoding::UTF8);

    auto p_default = map.to_position(4);
    ASSERT(p_default);
    EXPECT(p_default->character == 4U);

    auto p_utf16 = map.to_position(4, PositionEncoding::UTF16);
    ASSERT(p_utf16);
    EXPECT(p_utf16->character == 2U);
}

ZEST_CASE(to_range_basic) {
    std::string_view content = "abc\ndef";
    LineMap map(content, PositionEncoding::UTF8);

    auto range = map.to_range(0, 3);
    ASSERT(range);
    EXPECT(range->start.line == 0U);
    EXPECT(range->start.character == 0U);
    EXPECT(range->end.line == 0U);
    EXPECT(range->end.character == 3U);

    auto cross_line = map.to_range(0, 5);
    ASSERT(cross_line);
    EXPECT(cross_line->start.line == 0U);
    EXPECT(cross_line->end.line == 1U);
    EXPECT(cross_line->end.character == 1U);
}

ZEST_CASE(borrowed_line_starts) {
    std::string_view content = "ab\ncd";
    auto starts = build_line_starts(content);
    LineMap map(content, std::span<const std::uint32_t>(starts), PositionEncoding::UTF8);

    EXPECT(map.line_starts().data() == starts.data());
    EXPECT(map.line_starts().size() == starts.size());

    auto p = map.to_position(3);
    ASSERT(p);
    EXPECT(p->line == 1U);
    EXPECT(p->character == 0U);
}

ZEST_CASE(move_semantics) {
    std::string_view content = "ab\ncd";
    LineMap map(content, PositionEncoding::UTF8);

    LineMap moved(std::move(map));
    auto p = moved.to_position(3);
    ASSERT(p);
    EXPECT(p->line == 1U);
    EXPECT(p->character == 0U);

    LineMap assigned(std::string_view("x"));
    assigned = std::move(moved);
    auto p2 = assigned.to_position(4);
    ASSERT(p2);
    EXPECT(p2->line == 1U);
    EXPECT(p2->character == 1U);
}

};  // ZEST_SUITE(ipc_lsp_position)

}  // namespace
}  // namespace kota::ipc::lsp
