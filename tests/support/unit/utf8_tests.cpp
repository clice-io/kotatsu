#include <cstddef>
#include <string>
#include <string_view>

#include "kotatsu/zest/zest.h"
#include "kotatsu/support/utf8.h"

namespace kotatsu {

namespace {

using namespace std::literals;

ZEST_SUITE(support_utf8) {

ZEST_CASE(decode_reads_one_code_point) {
    ZSTATIC_EXPECT(decode_utf8("a").code_point == U'a');
    ZSTATIC_EXPECT(decode_utf8("\xC3\xA9!").code_point == U'\u00E9');
    ZSTATIC_EXPECT(decode_utf8("\xC3\xA9!").length == 2);
    ZSTATIC_EXPECT(decode_utf8("\xE2\x82\xAC").code_point == U'\u20AC');
    ZSTATIC_EXPECT(decode_utf8("\xF4\x8F\xBF\xBF").code_point == U'\U0010FFFF');
    ZSTATIC_EXPECT(decode_utf8("\xF4\x8F\xBF\xBF").length == 4);
}

ZEST_CASE(decode_takes_the_maximal_subpart_of_what_is_no_code_point) {
    // A code point cut short: its prefix, read as U+FFFD.
    constexpr auto cut = decode_utf8("\xE2\x82x");
    ZSTATIC_EXPECT(!cut.valid);
    ZSTATIC_EXPECT(cut.length == 2);
    ZSTATIC_EXPECT(cut.code_point == U'\uFFFD');
    // An overlong lead and the first byte of a surrogate start no prefix.
    ZSTATIC_EXPECT(decode_utf8("\xC0\xAF").length == 1);
    ZSTATIC_EXPECT(decode_utf8("\xED\xA0\x80").length == 1);
}

ZEST_CASE(utf8_accepts_whole_code_points) {
    ZEXPECT(is_utf8(""));
    ZEXPECT(is_utf8("ascii"));
    // U+00E9, U+20AC, U+10FFFF: two, three and four bytes.
    ZEXPECT(is_utf8("caf\xC3\xA9 \xE2\x82\xAC \xF4\x8F\xBF\xBF"));
    ZEXPECT(is_utf8("\0"sv));
    // Past a run of ASCII long enough to pass eight bytes at a time, and
    // inside such a word.
    ZEXPECT(is_utf8("eight bytes, then \xC3\xA9"));
    ZEXPECT(is_utf8("caf\xC3\xA9 and then more text"));
}

ZEST_CASE(utf8_rejects_what_is_no_code_point) {
    // A lone continuation byte, and a lead byte no code point starts with.
    ZEXPECT(!is_utf8("\x80"));
    ZEXPECT(!is_utf8("\xFF"));
    // A sequence cut short, at the end and before ASCII.
    ZEXPECT(!is_utf8("\xE2\x82"));
    ZEXPECT(!is_utf8("\xE2\x82x"));
    // Overlong forms of '/' and of U+0800.
    ZEXPECT(!is_utf8("\xC0\xAF"));
    ZEXPECT(!is_utf8("\xE0\x9F\xBF"));
    // A surrogate, U+D800, and U+110000, past the last code point.
    ZEXPECT(!is_utf8("\xED\xA0\x80"));
    ZEXPECT(!is_utf8("\xF4\x90\x80\x80"));
    ZEXPECT(!is_utf8("eight bytes, then \xE9"));
    ZEXPECT(!is_utf8("caf\xE9 and then more text"));
}

ZEST_CASE(utf8_checks_every_byte_of_a_text_of_any_size) {
    // ASCII passes in words; a byte past it anywhere goes to the full check.
    for(std::size_t size = 0; size <= 20; ++size) {
        ZEST_CONTEXT("size {}", size);
        const std::string text(size, 'a');
        ZEXPECT(is_utf8(text));
        for(std::size_t at = 0; at < size; ++at) {
            ZEST_CONTEXT("byte {}", at);
            auto broken = text;
            broken[at] = '\xFF';
            ZEXPECT(!is_utf8(broken));
            if(at + 1 < size) {
                auto accented = text;
                accented.replace(at, 2, "\xC3\xA9");
                ZEXPECT(is_utf8(accented));
            }
        }
    }
}

ZEST_CASE(replacement_takes_each_maximal_subpart) {
    ZEXPECT(replace_invalid_utf8("caf\xC3\xA9") == "caf\xC3\xA9");
    ZEXPECT(replace_invalid_utf8("caf\xE9!") == "caf\xEF\xBF\xBD!");
    // The prefix of a code point is one subpart; a byte that cannot extend
    // it starts the next.
    ZEXPECT(replace_invalid_utf8("\xE2\x82x") == "\xEF\xBF\xBDx");
    ZEXPECT(replace_invalid_utf8("\xF0\x9F\x98") == "\xEF\xBF\xBD");
    // A byte no prefix starts with is a subpart of its own: an overlong
    // lead, then each continuation byte after it.
    ZEXPECT(replace_invalid_utf8("\xC0\xAF") == "\xEF\xBF\xBD\xEF\xBF\xBD");
    ZEXPECT(replace_invalid_utf8("\xED\xA0\x80") == "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
}

ZEST_CASE(floor_char_boundary_keeps_a_boundary) {
    // "a", U+00E9, U+20AC, U+1F600: one, two, three and four bytes.
    constexpr std::string_view text = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
    ZSTATIC_EXPECT(floor_char_boundary(text, 0) == 0);
    ZSTATIC_EXPECT(floor_char_boundary(text, 1) == 1);
    ZSTATIC_EXPECT(floor_char_boundary(text, 3) == 3);
    ZSTATIC_EXPECT(floor_char_boundary(text, 6) == 6);
}

ZEST_CASE(floor_char_boundary_steps_back_to_the_sequence_start) {
    constexpr std::string_view text = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
    ZSTATIC_EXPECT(floor_char_boundary(text, 2) == 1);
    ZSTATIC_EXPECT(floor_char_boundary(text, 4) == 3);
    ZSTATIC_EXPECT(floor_char_boundary(text, 5) == 3);
    ZSTATIC_EXPECT(floor_char_boundary(text, 7) == 6);
    ZSTATIC_EXPECT(floor_char_boundary(text, 8) == 6);
    ZSTATIC_EXPECT(floor_char_boundary(text, 9) == 6);
}

ZEST_CASE(floor_char_boundary_past_the_end_is_the_size) {
    ZSTATIC_EXPECT(floor_char_boundary("ab", 2) == 2);
    ZSTATIC_EXPECT(floor_char_boundary("ab", 7) == 2);
    ZSTATIC_EXPECT(floor_char_boundary("", 0) == 0);
}

ZEST_CASE(floor_char_boundary_reads_what_is_no_utf8_as_the_decoder_does) {
    // The prefix of a code point cut short is one sequence.
    ZSTATIC_EXPECT(floor_char_boundary("\xE2\x82x", 1) == 0);
    ZSTATIC_EXPECT(floor_char_boundary("\xE2\x82x", 2) == 2);
    // A continuation byte past a whole code point is a sequence of its own.
    ZSTATIC_EXPECT(floor_char_boundary("\xC3\xA9\x80", 2) == 2);
    // So is each after an overlong lead, and each in a run longer than a
    // sequence can be.
    ZSTATIC_EXPECT(floor_char_boundary("\xC0\xAF", 1) == 1);
    ZSTATIC_EXPECT(floor_char_boundary("\x80\x80\x80\x80\x80", 4) == 4);
    ZSTATIC_EXPECT(floor_char_boundary("\x80\x80", 1) == 1);
}

};  // ZEST_SUITE(support_utf8)

}  // namespace

}  // namespace kotatsu
