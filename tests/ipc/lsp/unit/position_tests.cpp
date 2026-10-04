#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/ipc/lsp/position.h"

namespace kota::ipc::lsp {
namespace {

constexpr PositionEncoding encodings[] = {
    PositionEncoding::UTF8,
    PositionEncoding::UTF16,
    PositionEncoding::UTF32,
};

/// Whether a line of `content` ends in "\r\n", read from the text, for the
/// conversions over a text known by its size.
auto crlf_in(std::string_view content, const std::vector<std::uint32_t>& lines) {
    return [content, &lines](std::uint32_t line) {
        return line + 1 < lines.size() && lines[line + 1] >= 2 &&
               content[lines[line + 1] - 2] == '\r';
    };
}

// Throughout this suite: 你 is 3 UTF-8 bytes, 🙂 is 4.
ZEST_SUITE(ipc_lsp_position) {

ZEST_CASE(utf16_column_counts) {
    std::string_view content = "a你b\n";

    auto position = to_position(content, line_starts(content), 4, PositionEncoding::UTF16);
    ASSERT(position.has_value());
    EXPECT(*position == protocol::Position{.line = 0, .character = 2});
}

ZEST_CASE(position_offset_values) {
    std::string_view content = "a你🙂b\nx";
    auto lines = line_starts(content);

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
        for(auto [encoding, character]: {
                std::pair{PositionEncoding::UTF8,  sample.utf8_character },
                std::pair{PositionEncoding::UTF16, sample.utf16_character},
                std::pair{PositionEncoding::UTF32, sample.utf32_character},
        }) {
            ZEST_CONTEXT("encoding {}, offset {}", static_cast<int>(encoding), sample.offset);
            protocol::Position expected{.line = sample.line, .character = character};
            auto position = to_position(content, lines, sample.offset, encoding);
            ASSERT(position.has_value());
            EXPECT(*position == expected);
            EXPECT(to_offset(content, lines, expected, encoding) == sample.offset);
        }
    }
}

ZEST_CASE(boundaries_roundtrip) {
    std::string_view content = "a你b\nx🙂y\n\n🙂";
    auto lines = line_starts(content);
    constexpr std::uint32_t boundaries[] = {0, 1, 4, 5, 6, 7, 11, 12, 13, 14, 18};

    for(auto encoding: encodings) {
        for(auto offset: boundaries) {
            ZEST_CONTEXT("encoding {}, offset {}", static_cast<int>(encoding), offset);
            auto position = to_position(content, lines, offset, encoding);
            ASSERT(position.has_value());
            EXPECT(to_offset(content, lines, *position, encoding) == offset);
        }
    }
}

ZEST_CASE(line_of_finds_the_line_holding_an_offset) {
    std::string_view content = "ab\n\ncd";
    auto lines = line_starts(content);

    // A line's '\n' is the line's own.
    constexpr std::uint32_t holders[] = {0, 0, 0, 1, 2, 2, 2};
    for(std::uint32_t offset = 0; offset <= content.size(); ++offset) {
        ZEST_CONTEXT("offset {}", offset);
        EXPECT(line_of(lines, offset) == holders[offset]);
    }
}

ZEST_CASE(invalid_position_stability) {
    auto expect_stable = [&](std::string_view content) {
        auto lines = line_starts(content);
        for(auto encoding: encodings) {
            for(std::uint32_t offset = 0; offset <= content.size(); ++offset) {
                auto position = to_position(content, lines, offset, encoding);
                ASSERT(position);
                auto mapped_offset = to_offset(content, lines, *position, encoding);
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
    auto lines = line_starts(content);

    EXPECT(!to_position(content, lines, 100, PositionEncoding::UTF8).has_value());
    EXPECT(!to_position(content, lines, 8, PositionEncoding::UTF8).has_value());
    EXPECT(to_position(content, lines, 7, PositionEncoding::UTF8).has_value());
}

ZEST_CASE(to_offset_past_the_last_line_fails) {
    std::string_view content = "abc\ndef";
    auto lines = line_starts(content);

    for(std::uint32_t line: {2U, 5U}) {
        ZEST_CONTEXT("line {}", line);
        protocol::Position position{.line = line, .character = 0};
        EXPECT(to_offset(content, lines, position, PositionEncoding::UTF8) == std::nullopt);
    }
    EXPECT(to_offset(content, lines, {.line = 1, .character = 0}, PositionEncoding::UTF8) == 4U);
}

// LSP 3.17: a character past the line's length defaults back to it.
ZEST_CASE(to_offset_past_the_line_end_clamps_to_it) {
    std::string_view content = "abc\ndef";
    auto lines = line_starts(content);

    for(auto encoding: encodings) {
        ZEST_CONTEXT("encoding: {}", static_cast<int>(encoding));
        EXPECT(to_offset(content, lines, {.line = 0, .character = 3}, encoding) == 3U);
        EXPECT(to_offset(content, lines, {.line = 0, .character = 10}, encoding) == 3U);
        EXPECT(to_offset(content, lines, {.line = 1, .character = 4}, encoding) == 7U);
    }
}

ZEST_CASE(to_offset_past_a_non_ascii_line_end_clamps_to_it) {
    std::string_view content = "a你b\r\nc";
    auto lines = line_starts(content);

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
        for(auto character: {sample.length, sample.length + 1, 99U}) {
            ZEST_CONTEXT("encoding {}, character {}", static_cast<int>(sample.encoding), character);
            protocol::Position position{.line = 0, .character = character};
            EXPECT(to_offset(content, lines, position, sample.encoding) == 5U);
        }
    }
}

ZEST_CASE(crlf_ends_a_line) {
    std::string_view content = "ab\r\ncd";
    auto lines = line_starts(content);

    auto inside = to_position(content, lines, 3, PositionEncoding::UTF16);
    ASSERT(inside.has_value());
    EXPECT(*inside == protocol::Position{.line = 0, .character = 2});
    EXPECT(to_offset(content, lines, {.line = 0, .character = 3}, PositionEncoding::UTF16) == 2U);
}

// Line starts stay what line_starts gives, which callers persist.
ZEST_CASE(lone_cr_is_text) {
    std::string_view content = "a\rb\nc";
    auto lines = line_starts(content);

    EXPECT(lines == std::vector<std::uint32_t>{0, 4});
    auto after_cr = to_position(content, lines, 2, PositionEncoding::UTF16);
    ASSERT(after_cr.has_value());
    EXPECT(*after_cr == protocol::Position{.line = 0, .character = 2});
    EXPECT(to_offset(content, lines, {.line = 0, .character = 9}, PositionEncoding::UTF16) == 3U);
}

// UTF-16 unit 1 of "🙂" is the second half of its surrogate pair.
ZEST_CASE(to_offset_inside_a_surrogate_pair_fails) {
    std::string_view content = "🙂";

    EXPECT(to_offset(content,
                     line_starts(content),
                     {.line = 0, .character = 1},
                     PositionEncoding::UTF16) == std::nullopt);
}

// Bytes 2 to 4 of "a🙂b" are inside 🙂, which starts at byte 1.
ZEST_CASE(to_offset_inside_a_utf8_sequence_fails) {
    std::string_view content = "a🙂b";
    auto lines = line_starts(content);

    for(std::uint32_t character: {2U, 3U, 4U}) {
        ZEST_CONTEXT("character {}", character);
        protocol::Position position{.line = 0, .character = character};
        EXPECT(to_offset(content, lines, position, PositionEncoding::UTF8) == std::nullopt);
    }
    EXPECT(to_offset(content, lines, {.line = 0, .character = 1}, PositionEncoding::UTF8) == 1U);
    EXPECT(to_offset(content, lines, {.line = 0, .character = 5}, PositionEncoding::UTF8) == 5U);
}

ZEST_CASE(to_position_inside_a_code_point_is_at_its_start) {
    std::string_view content = "a🙂b";
    auto lines = line_starts(content);

    for(auto encoding: encodings) {
        for(std::uint32_t offset: {2U, 3U, 4U}) {
            ZEST_CONTEXT("encoding {}, offset {}", static_cast<int>(encoding), offset);
            auto position = to_position(content, lines, offset, encoding);
            ASSERT(position.has_value());
            EXPECT(*position == protocol::Position{.line = 0, .character = 1});
        }
    }
}

// So a range never runs backwards, whatever bytes its ends fall on.
ZEST_CASE(to_position_never_decreases_with_the_offset) {
    std::string_view content = "a你b\r\n🙂x\n\xF0\x9F\x99y";
    auto lines = line_starts(content);

    for(auto encoding: encodings) {
        protocol::Position last{};
        for(std::uint32_t offset = 0; offset <= content.size(); ++offset) {
            ZEST_CONTEXT("encoding {}, offset {}", static_cast<int>(encoding), offset);
            auto position = to_position(content, lines, offset, encoding);
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
    auto lines = line_starts(content);

    for(auto position: {
            protocol::Position{.line = 2, .character = 0},
            protocol::Position{.line = 9, .character = 4}
    }) {
        ZEST_CONTEXT("{}:{}", position.line, position.character);
        EXPECT(to_offset_clamped(content, lines, position, PositionEncoding::UTF16) == 7U);
    }
}

ZEST_CASE(to_offset_clamped_inside_a_code_point_is_its_start) {
    std::string_view content = "a🙂b";
    auto lines = line_starts(content);

    protocol::Position inside_pair{.line = 0, .character = 2};
    EXPECT(to_offset_clamped(content, lines, inside_pair, PositionEncoding::UTF16) == 1U);
    for(std::uint32_t character: {2U, 3U, 4U}) {
        ZEST_CONTEXT("character {}", character);
        protocol::Position position{.line = 0, .character = character};
        EXPECT(to_offset_clamped(content, lines, position, PositionEncoding::UTF8) == 1U);
    }
}

// Where to_offset has an offset, to_offset_clamped has the same; elsewhere,
// one to_position and to_offset take back unchanged.
ZEST_CASE(to_offset_clamped_lands_on_a_place_in_the_text) {
    std::string_view content = "a你🙂b\r\nxy\n\n🙂";
    auto lines = line_starts(content);

    for(auto encoding: encodings) {
        for(std::uint32_t line = 0; line <= lines.size(); ++line) {
            for(std::uint32_t character = 0; character < 14; ++character) {
                ZEST_CONTEXT("encoding {}, {}:{}", static_cast<int>(encoding), line, character);
                protocol::Position position{.line = line, .character = character};
                auto clamped = to_offset_clamped(content, lines, position, encoding);
                if(auto offset = to_offset(content, lines, position, encoding)) {
                    EXPECT(clamped == *offset);
                    continue;
                }
                auto back = to_position(content, lines, clamped, encoding);
                ASSERT(back.has_value());
                EXPECT(to_offset(content, lines, *back, encoding) == clamped);
            }
        }
    }
}

ZEST_CASE(to_range_converts_both_ends) {
    std::string_view content = "abc\ndef";

    auto range = to_range(content, line_starts(content), 1, 5, PositionEncoding::UTF8);
    ASSERT(range.has_value());
    EXPECT(*range == protocol::Range{
                         .start = {.line = 0, .character = 1},
                         .end = {.line = 1, .character = 1}
    });
}

ZEST_CASE(to_range_running_backwards_fails) {
    std::string_view content = "abc\ndef";

    EXPECT(!to_range(content, line_starts(content), 5, 1, PositionEncoding::UTF8).has_value());
}

ZEST_CASE(to_offset_range_clamps_each_end) {
    std::string_view content = "a🙂b\ncd";

    protocol::Range range{
        .start = {.line = 0, .character = 2},
        .end = {.line = 7, .character = 0}
    };
    auto offsets = to_offset_range(content, line_starts(content), range, PositionEncoding::UTF16);
    EXPECT(offsets.begin == 1U);
    EXPECT(offsets.end == 9U);
}

// As vscode-languageserver-textdocument reads a range whose start is past its
// end: the text between them.
ZEST_CASE(to_offset_range_reads_a_backward_range_forwards) {
    std::string_view content = "abc\ndef";

    protocol::Range range{
        .start = {.line = 1, .character = 1},
        .end = {.line = 0, .character = 2}
    };
    auto offsets = to_offset_range(content, line_starts(content), range, PositionEncoding::UTF16);
    EXPECT(offsets.begin == 2U);
    EXPECT(offsets.end == 5U);
}

// Line starts are any random-access range of unsigned integers, a view that
// computes each start included.
ZEST_CASE(line_tables_of_any_unsigned_range_convert_alike) {
    std::string_view content = "a你b\r\n🙂x\n\nxyz";
    auto lines = line_starts(content);
    std::vector<std::uint16_t> narrow(lines.begin(), lines.end());
    // Each start as the start of its block of two lines and an offset into it.
    std::vector<std::uint32_t> bases;
    std::vector<std::uint8_t> offsets;
    for(std::size_t line = 0; line < lines.size(); ++line) {
        if(line % 2 == 0) {
            bases.push_back(lines[line]);
        }
        offsets.push_back(static_cast<std::uint8_t>(lines[line] - bases.back()));
    }
    auto blocked = std::views::iota(std::size_t{0}, lines.size()) |
                   std::views::transform([&](std::size_t line) -> std::uint32_t {
                       return bases[line / 2] + offsets[line];
                   });

    for(auto encoding: encodings) {
        for(std::uint32_t offset = 0; offset <= content.size(); ++offset) {
            ZEST_CONTEXT("encoding {}, offset {}", static_cast<int>(encoding), offset);
            auto expected = to_position(content, lines, offset, encoding);
            ASSERT(expected.has_value());
            auto expect_position_over = [&](const auto& table) {
                auto position = to_position(content, table, offset, encoding);
                ASSERT(position.has_value());
                EXPECT(*position == *expected);
            };
            expect_position_over(std::span(lines));
            expect_position_over(narrow);
            expect_position_over(blocked);
            EXPECT(to_offset(content, blocked, *expected, encoding) == offset);
        }
    }
}

// Lines known to hold only ASCII are not read: one said to, though it does
// not, counts its bytes as units.
ZEST_CASE(lines_known_ascii_count_bytes) {
    std::string_view content = "a你b\nxy";
    auto lines = line_starts(content);

    auto position = to_position(content, lines, 4, PositionEncoding::UTF16, all_ascii);
    ASSERT(position.has_value());
    EXPECT(*position == protocol::Position{.line = 0, .character = 4});
    protocol::Position second{.line = 0, .character = 2};
    EXPECT(to_offset(content, lines, second, PositionEncoding::UTF16, all_ascii) == 2U);
}

// Whatever the caller knows, and in whichever form, if it is true the text
// converts as one read throughout.
ZEST_CASE(true_ascii_knowledge_converts_as_the_text_reads) {
    std::string_view content = "abc\r\na你b\n\n🙂z\nxyz";
    auto lines = line_starts(content);
    auto bits = non_ascii_lines(content);
    auto by_line = [](std::uint32_t line) {
        return line != 1 && line != 3;
    };

    for(auto encoding: encodings) {
        for(std::uint32_t offset = 0; offset <= content.size(); ++offset) {
            ZEST_CONTEXT("encoding {}, offset {}", static_cast<int>(encoding), offset);
            auto expected = to_position(content, lines, offset, encoding, unknown_ascii);
            ASSERT(expected.has_value());
            auto expect_position_knowing = [&](const auto& ascii) {
                auto position = to_position(content, lines, offset, encoding, ascii);
                ASSERT(position.has_value());
                EXPECT(*position == *expected);
            };
            expect_position_knowing(bits);
            expect_position_knowing(by_line);
        }
        for(std::uint32_t line = 0; line <= lines.size(); ++line) {
            for(std::uint32_t character = 0; character < 8; ++character) {
                ZEST_CONTEXT("encoding {}, {}:{}", static_cast<int>(encoding), line, character);
                protocol::Position position{.line = line, .character = character};
                auto expected = to_offset_clamped(content, lines, position, encoding);
                EXPECT(to_offset_clamped(content, lines, position, encoding, bits) == expected);
                EXPECT(to_offset_clamped(content, lines, position, encoding, by_line) == expected);
            }
        }
    }
}

// non_ascii_lines keeps a bit per line, 64 to a word.
ZEST_CASE(non_ascii_lines_far_apart_convert_by_their_text) {
    std::string content;
    for(int line = 0; line < 200; ++line) {
        content += (line == 70 || line == 130) ? "a你b\n" : "abcde\n";
    }
    auto lines = line_starts(content);
    auto bits = non_ascii_lines(content);

    for(std::uint32_t line: {69U, 70U, 71U, 129U, 130U, 199U}) {
        ZEST_CONTEXT("line {}", line);
        const bool wide = line == 70 || line == 130;
        auto end = to_position(content, lines, lines[line] + 4, PositionEncoding::UTF16, bits);
        ASSERT(end.has_value());
        EXPECT(*end == protocol::Position{.line = line, .character = wide ? 2U : 4U});
        protocol::Position second{.line = line, .character = 2};
        EXPECT(to_offset(content, lines, second, PositionEncoding::UTF16, bits) ==
               lines[line] + (wide ? 4U : 2U));
    }
}

ZEST_CASE(one_shot_conversions_find_the_line_starts) {
    std::string_view content = "a你b\r\n🙂x\n\nxyz";
    auto lines = line_starts(content);

    for(auto encoding: encodings) {
        for(std::uint32_t offset = 0; offset <= content.size(); ++offset) {
            ZEST_CONTEXT("encoding {}, offset {}", static_cast<int>(encoding), offset);
            auto once = to_position(content, offset, encoding);
            auto kept = to_position(content, lines, offset, encoding);
            ASSERT(once.has_value());
            ASSERT(kept.has_value());
            EXPECT(*once == *kept);
            auto range_once = to_range(content, 0, offset, encoding);
            auto range_kept = to_range(content, lines, 0, offset, encoding);
            ASSERT(range_once.has_value());
            ASSERT(range_kept.has_value());
            EXPECT(*range_once == *range_kept);
        }
        auto past_the_end = static_cast<std::uint32_t>(content.size() + 1);
        EXPECT(!to_position(content, past_the_end, encoding).has_value());
        EXPECT(!to_range(content, 0, past_the_end, encoding).has_value());
        for(std::uint32_t line = 0; line <= lines.size(); ++line) {
            for(std::uint32_t character = 0; character < 8; ++character) {
                ZEST_CONTEXT("encoding {}, {}:{}", static_cast<int>(encoding), line, character);
                protocol::Position position{.line = line, .character = character};
                EXPECT(to_offset(content, position, encoding) ==
                       to_offset(content, lines, position, encoding));
                EXPECT(to_offset_clamped(content, position, encoding) ==
                       to_offset_clamped(content, lines, position, encoding));
                protocol::Range range{
                    .start = position,
                    .end = {.line = 1, .character = 1}
                };
                auto once = to_offset_range(content, range, encoding);
                auto kept = to_offset_range(content, lines, range, encoding);
                EXPECT(once.begin == kept.begin);
                EXPECT(once.end == kept.end);
            }
        }
    }
}

// An ASCII text known by its size and line starts converts as its text does,
// told which lines end in "\r\n".
ZEST_CASE(sized_text_converts_as_its_text_does) {
    std::string_view content = "ab\r\n\r\ncd\nef\r\ng";
    auto lines = line_starts(content);
    auto size = static_cast<std::uint32_t>(content.size());
    auto crlf = crlf_in(content, lines);

    for(std::uint32_t offset = 0; offset <= size; ++offset) {
        ZEST_CONTEXT("offset {}", offset);
        auto sized = to_position(size, lines, offset, crlf);
        auto read = to_position(content, lines, offset, PositionEncoding::UTF16);
        ASSERT(sized.has_value());
        ASSERT(read.has_value());
        EXPECT(*sized == *read);
        auto sized_range = to_range(size, lines, 0, offset, crlf);
        auto read_range = to_range(content, lines, 0, offset, PositionEncoding::UTF16);
        ASSERT(sized_range.has_value());
        ASSERT(read_range.has_value());
        EXPECT(*sized_range == *read_range);
    }
    EXPECT(!to_position(size, lines, size + 1, crlf).has_value());
    for(std::uint32_t line = 0; line <= lines.size(); ++line) {
        for(std::uint32_t character = 0; character < 5; ++character) {
            ZEST_CONTEXT("{}:{}", line, character);
            protocol::Position position{.line = line, .character = character};
            EXPECT(to_offset(size, lines, position, crlf) ==
                   to_offset(content, lines, position, PositionEncoding::UTF16));
            EXPECT(to_offset_clamped(size, lines, position, crlf) ==
                   to_offset_clamped(content, lines, position, PositionEncoding::UTF16));
            protocol::Range range{
                .start = position,
                .end = {.line = 0, .character = 1}
            };
            auto sized = to_offset_range(size, lines, range, crlf);
            auto read = to_offset_range(content, lines, range, PositionEncoding::UTF16);
            EXPECT(sized.begin == read.begin);
            EXPECT(sized.end == read.end);
        }
    }
}

// Without the text, only the caller says whether a '\r' ends a line.
ZEST_CASE(sized_text_ends_lines_where_told) {
    std::string_view content = "ab\r\ncd";
    auto lines = line_starts(content);
    auto size = static_cast<std::uint32_t>(content.size());
    protocol::Position past_the_end{.line = 0, .character = 9};

    EXPECT(to_offset(size, lines, past_the_end, [](std::uint32_t) { return true; }) == 2U);
    EXPECT(to_offset(size, lines, past_the_end, [](std::uint32_t) { return false; }) == 3U);
}

};  // ZEST_SUITE(ipc_lsp_position)

}  // namespace
}  // namespace kota::ipc::lsp
