#include <string_view>

#include "kota/zest/zest.h"
#include "kota/codec/visit/common.h"

namespace kota::codec {

namespace {

using namespace std::literals;

ZEST_SUITE(codec_visit_common) {

ZEST_CASE(utf8_accepts_whole_code_points) {
    EXPECT(is_utf8(""));
    EXPECT(is_utf8("ascii"));
    // U+00E9, U+20AC, U+10FFFF: two, three and four bytes.
    EXPECT(is_utf8("caf\xC3\xA9 \xE2\x82\xAC \xF4\x8F\xBF\xBF"));
    EXPECT(is_utf8("\0"sv));
    // Past a run of ASCII long enough to pass eight bytes at a time, and
    // inside such a word.
    EXPECT(is_utf8("eight bytes, then \xC3\xA9"));
    EXPECT(is_utf8("caf\xC3\xA9 and then more text"));
}

ZEST_CASE(utf8_rejects_what_is_no_code_point) {
    // A lone continuation byte, and a lead byte no code point starts with.
    EXPECT(!is_utf8("\x80"));
    EXPECT(!is_utf8("\xFF"));
    // A sequence cut short, at the end and before ASCII.
    EXPECT(!is_utf8("\xE2\x82"));
    EXPECT(!is_utf8("\xE2\x82x"));
    // Overlong forms of '/' and of U+0800.
    EXPECT(!is_utf8("\xC0\xAF"));
    EXPECT(!is_utf8("\xE0\x9F\xBF"));
    // A surrogate, U+D800, and U+110000, past the last code point.
    EXPECT(!is_utf8("\xED\xA0\x80"));
    EXPECT(!is_utf8("\xF4\x90\x80\x80"));
    EXPECT(!is_utf8("eight bytes, then \xE9"));
    EXPECT(!is_utf8("caf\xE9 and then more text"));
}

ZEST_CASE(replacement_takes_each_maximal_subpart) {
    EXPECT(replace_invalid_utf8("caf\xC3\xA9") == "caf\xC3\xA9");
    EXPECT(replace_invalid_utf8("caf\xE9!") == "caf\xEF\xBF\xBD!");
    // The prefix of a code point is one subpart; a byte that cannot extend
    // it starts the next.
    EXPECT(replace_invalid_utf8("\xE2\x82x") == "\xEF\xBF\xBDx");
    EXPECT(replace_invalid_utf8("\xF0\x9F\x98") == "\xEF\xBF\xBD");
    // A byte no prefix starts with is a subpart of its own: an overlong
    // lead, then each continuation byte after it.
    EXPECT(replace_invalid_utf8("\xC0\xAF") == "\xEF\xBF\xBD\xEF\xBF\xBD");
    EXPECT(replace_invalid_utf8("\xED\xA0\x80") == "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
}

};  // ZEST_SUITE(codec_visit_common)

}  // namespace

}  // namespace kota::codec
