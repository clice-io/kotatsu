#include <cstdint>

#include "kota/zest/zest.h"
#include "kota/ipc/lsp/position.h"

namespace kota::ipc::lsp {
namespace {

TEST_SUITE(language_position) {

TEST_CASE(parse_encoding_values) {
    EXPECT_EQ(parse_position_encoding(protocol::PositionEncodingKind::utf8),
              PositionEncoding::UTF8);
    EXPECT_EQ(parse_position_encoding(protocol::PositionEncodingKind::utf16),
              PositionEncoding::UTF16);
    EXPECT_EQ(parse_position_encoding(protocol::PositionEncodingKind::utf32),
              PositionEncoding::UTF32);
    EXPECT_EQ(parse_position_encoding("unknown-encoding"), PositionEncoding::UTF16);
}

TEST_CASE(utf16_column_counts) {
    std::string_view content = "a\xe4\xbd\xa0" "b\n";
    PositionMapper converter(content, PositionEncoding::UTF16);

    auto position = converter.to_position(4);
    ASSERT_TRUE(position.has_value());
    ASSERT_EQ(position->line, 0U);
    ASSERT_EQ(position->character, 2U);
}

TEST_CASE(round_trip_offsets) {
    std::string_view content = "a\xe4\xbd\xa0" "b\nx\xf0\x9f\x99\x82" "y";
    constexpr std::uint32_t offsets[] = {0, 1, 4, 5, 6, 7, 11, 12};

    for(auto encoding: {PositionEncoding::UTF8, PositionEncoding::UTF16, PositionEncoding::UTF32}) {
        PositionMapper converter(content, encoding);
        for(auto offset: offsets) {
            auto position = converter.to_position(offset);
            ASSERT_TRUE(position.has_value());
            auto mapped = converter.to_offset(*position);
            ASSERT_TRUE(mapped.has_value());
            ASSERT_EQ(*mapped, offset);
        }
    }
}

TEST_CASE(position_offset_values) {
    std::string_view content = "a\xe4\xbd\xa0\xf0\x9f\x99\x82" "b\nx";

    PositionMapper utf8_converter(content, PositionEncoding::UTF8);
    PositionMapper utf16_converter(content, PositionEncoding::UTF16);
    PositionMapper utf32_converter(content, PositionEncoding::UTF32);

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

    for(const auto& sample: samples) {
        auto p8 = utf8_converter.to_position(sample.offset);
        ASSERT_TRUE(p8.has_value());
        EXPECT_EQ(p8->line, sample.line);
        EXPECT_EQ(p8->character, sample.utf8_character);
        auto o8 = utf8_converter.to_offset(*p8);
        ASSERT_TRUE(o8.has_value());
        EXPECT_EQ(*o8, sample.offset);

        auto p16 = utf16_converter.to_position(sample.offset);
        ASSERT_TRUE(p16.has_value());
        EXPECT_EQ(p16->line, sample.line);
        EXPECT_EQ(p16->character, sample.utf16_character);
        auto o16 = utf16_converter.to_offset(*p16);
        ASSERT_TRUE(o16.has_value());
        EXPECT_EQ(*o16, sample.offset);

        auto p32 = utf32_converter.to_position(sample.offset);
        ASSERT_TRUE(p32.has_value());
        EXPECT_EQ(p32->line, sample.line);
        EXPECT_EQ(p32->character, sample.utf32_character);
        auto o32 = utf32_converter.to_offset(*p32);
        ASSERT_TRUE(o32.has_value());
        EXPECT_EQ(*o32, sample.offset);
    }
}

TEST_CASE(line_helpers_lines) {
    std::string_view content = "ab\n\ncd";
    PositionMapper converter(content, PositionEncoding::UTF8);

    EXPECT_EQ(converter.line_start(0), 0U);
    EXPECT_EQ(converter.line_end_exclusive(0), 2U);
    EXPECT_EQ(converter.line_start(1), 3U);
    EXPECT_EQ(converter.line_end_exclusive(1), 3U);
    EXPECT_EQ(converter.line_start(2), 4U);
    EXPECT_EQ(converter.line_end_exclusive(2), 6U);

    EXPECT_EQ(converter.line_of(0), 0U);
    EXPECT_EQ(converter.line_of(2), 0U);
    EXPECT_EQ(converter.line_of(3), 1U);
    EXPECT_EQ(converter.line_of(4), 2U);
    EXPECT_EQ(converter.line_of(6), 2U);
}

TEST_CASE(measure_units_encoding) {
    std::string_view content = "a\xe4\xbd\xa0\xf0\x9f\x99\x82z";

    PositionMapper utf8_converter(content, PositionEncoding::UTF8);
    PositionMapper utf16_converter(content, PositionEncoding::UTF16);
    PositionMapper utf32_converter(content, PositionEncoding::UTF32);

    EXPECT_EQ(utf8_converter.measure(content), 9U);
    EXPECT_EQ(utf16_converter.measure(content), 5U);
    EXPECT_EQ(utf32_converter.measure(content), 4U);
}

TEST_CASE(character_length_units) {
    std::string_view content = "a\xe4\xbd\xa0\xf0\x9f\x99\x82z\n";

    PositionMapper utf8_converter(content, PositionEncoding::UTF8);
    PositionMapper utf16_converter(content, PositionEncoding::UTF16);
    PositionMapper utf32_converter(content, PositionEncoding::UTF32);

    EXPECT_EQ(utf8_converter.character(0, 9), 9U);
    EXPECT_EQ(utf16_converter.character(0, 9), 5U);
    EXPECT_EQ(utf32_converter.character(0, 9), 4U);

    EXPECT_EQ(utf8_converter.length(0, 1, 8), 7U);
    EXPECT_EQ(utf16_converter.length(0, 1, 8), 3U);
    EXPECT_EQ(utf32_converter.length(0, 1, 8), 2U);

    EXPECT_EQ(utf8_converter.length(0, 8, 8), 0U);
    EXPECT_EQ(utf16_converter.length(0, 8, 8), 0U);
    EXPECT_EQ(utf32_converter.length(0, 8, 8), 0U);
}

TEST_CASE(roundtrip_multiline_boundaries) {
    std::string_view content = "a\xe4\xbd\xa0\n\xf0\x9f\x99\x82" "b";
    constexpr std::uint32_t boundaries[] = {0, 1, 4, 5, 9, 10};

    for(auto encoding: {PositionEncoding::UTF8, PositionEncoding::UTF16, PositionEncoding::UTF32}) {
        PositionMapper converter(content, encoding);
        for(auto offset: boundaries) {
            auto position = converter.to_position(offset);
            ASSERT_TRUE(position.has_value());
            auto mapped = converter.to_offset(*position);
            ASSERT_TRUE(mapped.has_value());
            ASSERT_EQ(*mapped, offset);
        }
    }
}

TEST_CASE(invalid_continuation_progress) {
    auto expect_progress = [&](auto... bytes) {
        const char raw[] = {static_cast<char>(bytes)...};
        constexpr auto length = static_cast<std::uint32_t>(sizeof...(bytes));
        auto content = std::string_view(raw, sizeof...(bytes));

        PositionMapper utf8_converter(content, PositionEncoding::UTF8);
        PositionMapper utf16_converter(content, PositionEncoding::UTF16);
        PositionMapper utf32_converter(content, PositionEncoding::UTF32);

        EXPECT_EQ(utf8_converter.measure(content), length);
        EXPECT_EQ(utf16_converter.measure(content), length);
        EXPECT_EQ(utf32_converter.measure(content), length);
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

TEST_CASE(invalid_position_stability) {
    auto expect_stable = [&](std::string_view content) {
        for(auto encoding:
            {PositionEncoding::UTF8, PositionEncoding::UTF16, PositionEncoding::UTF32}) {
            PositionMapper converter(content, encoding);
            for(std::uint32_t offset = 0; offset <= content.size(); ++offset) {
                auto position = converter.to_position(offset);
                ASSERT_TRUE(position.has_value());
                auto mapped_offset = converter.to_offset(*position);
                ASSERT_TRUE(mapped_offset.has_value());
                EXPECT_TRUE(*mapped_offset <= content.size());
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

TEST_CASE(strict_utf8_validation) {
    PositionMapper utf16_converter("", PositionEncoding::UTF16);
    PositionMapper utf32_converter("", PositionEncoding::UTF32);

    auto expect_invalid_sequence = [&](auto... bytes) {
        const char raw[] = {static_cast<char>(bytes)...};
        constexpr auto length = static_cast<std::uint32_t>(sizeof...(bytes));
        auto content = std::string_view(raw, sizeof...(bytes));

        EXPECT_EQ(utf16_converter.measure(content), length);
        EXPECT_EQ(utf32_converter.measure(content), length);
    };

    expect_invalid_sequence(0xC0u, 0x80u);
    expect_invalid_sequence(0xE0u, 0x80u, 0x80u);
    expect_invalid_sequence(0xEDu, 0xA0u, 0x80u);
    expect_invalid_sequence(0xF4u, 0x90u, 0x80u, 0x80u);
    expect_invalid_sequence(0xF5u, 0x80u, 0x80u, 0x80u);
    expect_invalid_sequence('a', 0xF0u, 0x9Fu, 'b');
}

TEST_CASE(to_position_out_of_range) {
    std::string_view content = "abc\ndef";
    PositionMapper converter(content, PositionEncoding::UTF8);

    // Offset beyond content size.
    EXPECT_FALSE(converter.to_position(100).has_value());
    EXPECT_FALSE(converter.to_position(8).has_value());

    // Offset at content size is valid (EOF position).
    EXPECT_TRUE(converter.to_position(7).has_value());
}

TEST_CASE(to_offset_line_out_of_range) {
    std::string_view content = "abc\ndef";
    PositionMapper converter(content, PositionEncoding::UTF8);

    // Line beyond document.
    EXPECT_FALSE(converter.to_offset({.line = 5, .character = 0}).has_value());
    EXPECT_FALSE(converter.to_offset({.line = 2, .character = 0}).has_value());

    // Valid last line.
    EXPECT_TRUE(converter.to_offset({.line = 1, .character = 0}).has_value());
}

TEST_CASE(to_offset_character_out_of_range) {
    std::string_view content = "abc\ndef";

    for(auto encoding: {PositionEncoding::UTF8, PositionEncoding::UTF16, PositionEncoding::UTF32}) {
        PositionMapper converter(content, encoding);

        // Character beyond line length.
        EXPECT_FALSE(converter.to_offset({.line = 0, .character = 10}).has_value());
        EXPECT_FALSE(converter.to_offset({.line = 1, .character = 4}).has_value());

        // Valid end of line.
        EXPECT_TRUE(converter.to_offset({.line = 0, .character = 3}).has_value());
        EXPECT_TRUE(converter.to_offset({.line = 1, .character = 3}).has_value());
    }
}

};  // TEST_SUITE(language_position)

}  // namespace
}  // namespace kota::ipc::lsp
