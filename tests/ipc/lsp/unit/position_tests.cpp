#include <cstdint>

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

ZEST_CASE(round_trip_offsets) {
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

ZEST_CASE(measure_units_encoding) {
    std::string_view content = "a你🙂z";

    EXPECT(encoded_length(content, PositionEncoding::UTF8) == 9U);
    EXPECT(encoded_length(content, PositionEncoding::UTF16) == 5U);
    EXPECT(encoded_length(content, PositionEncoding::UTF32) == 4U);
}

ZEST_CASE(roundtrip_multiline_boundaries) {
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

ZEST_CASE(invalid_continuation_progress) {
    auto expect_progress = [&](auto... bytes) {
        const char raw[] = {static_cast<char>(bytes)...};
        constexpr auto len = static_cast<std::uint32_t>(sizeof...(bytes));
        auto content = std::string_view(raw, sizeof...(bytes));

        EXPECT(encoded_length(content, PositionEncoding::UTF8) == len);
        EXPECT(encoded_length(content, PositionEncoding::UTF16) == len);
        EXPECT(encoded_length(content, PositionEncoding::UTF32) == len);
    };

    // 3-byte lead with invalid second byte.
    expect_progress('a', 0xE4u, 'X', 'b');
    // 2-byte lead with invalid continuation byte.
    expect_progress(0xC2u, 'A');
    // 3-byte lead with invalid third byte.
    expect_progress(0xE1u, 0x80u, 'B');
    // 4-byte lead with invalid second byte.
    expect_progress(0xF1u, 'C', 0x80u, 0x80u);
    // 4-byte lead with invalid fourth byte.
    expect_progress(0xF1u, 0x80u, 0x80u, 'D');
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

ZEST_CASE(strict_utf8_validation) {
    auto expect_invalid_sequence = [&](auto... bytes) {
        const char raw[] = {static_cast<char>(bytes)...};
        constexpr auto len = static_cast<std::uint32_t>(sizeof...(bytes));
        auto content = std::string_view(raw, sizeof...(bytes));

        EXPECT(encoded_length(content, PositionEncoding::UTF16) == len);
        EXPECT(encoded_length(content, PositionEncoding::UTF32) == len);
    };

    expect_invalid_sequence(0xC0u, 0x80u);
    expect_invalid_sequence(0xE0u, 0x80u, 0x80u);
    expect_invalid_sequence(0xEDu, 0xA0u, 0x80u);
    expect_invalid_sequence(0xF4u, 0x90u, 0x80u, 0x80u);
    expect_invalid_sequence(0xF5u, 0x80u, 0x80u, 0x80u);
    expect_invalid_sequence('a', 0xF0u, 0x9Fu, 'b');
}

ZEST_CASE(to_position_out_of_range) {
    std::string_view content = "abc\ndef";
    LineMap map(content, PositionEncoding::UTF8);

    EXPECT(!map.to_position(100).has_value());
    EXPECT(!map.to_position(8).has_value());
    EXPECT(map.to_position(7).has_value());
}

ZEST_CASE(to_offset_line_out_of_range) {
    std::string_view content = "abc\ndef";
    LineMap map(content, PositionEncoding::UTF8);

    EXPECT(!map.to_offset({.line = 5, .character = 0}).has_value());
    EXPECT(!map.to_offset({.line = 2, .character = 0}).has_value());
    EXPECT(map.to_offset({.line = 1, .character = 0}).has_value());
}

ZEST_CASE(to_offset_character_out_of_range) {
    std::string_view content = "abc\ndef";

    for(auto encoding: {PositionEncoding::UTF8, PositionEncoding::UTF16, PositionEncoding::UTF32}) {
        LineMap map(content, encoding);

        EXPECT(!map.to_offset({.line = 0, .character = 10}).has_value());
        EXPECT(!map.to_offset({.line = 1, .character = 4}).has_value());
        EXPECT(map.to_offset({.line = 0, .character = 3}).has_value());
        EXPECT(map.to_offset({.line = 1, .character = 3}).has_value());
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
