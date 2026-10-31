#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#include "kotatsu/zest/zest.h"
#include "kotatsu/support/small_string.h"
#include "kotatsu/support/small_vector.h"
#include "kotatsu/support/utf16.h"

namespace kotatsu {

namespace {

using namespace std::literals;

/// What from_utf16() writes for `text`.
std::string narrow(std::u16string_view text) {
    std::string out;
    from_utf16(out, text);
    return out;
}

ZEST_SUITE(support_utf16) {

ZEST_CASE(to_utf16_converts_code_points_of_every_length) {
    std::u16string wide;
    // U+0041, U+00E9, U+4E2D, U+1F600: one to four bytes, the last a pair.
    ZEXPECT(!to_utf16(wide, "A\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80"));
    ZEXPECT(wide == u"Aé中\U0001F600");
    ZEXPECT(!to_utf16(wide, ""));
    ZEXPECT(wide == u"");
    ZEXPECT(!to_utf16(wide, "a\0b"sv));
    ZEXPECT(wide == u"a\0b"sv);
}

ZEST_CASE(to_utf16_reads_a_lone_surrogate_in_three_bytes) {
    std::u16string wide;
    ZEXPECT(!to_utf16(wide, "\xED\xA0\x80"));
    ZEXPECT(wide == u"\xD800");
    ZEXPECT(!to_utf16(wide, "a\xED\xBF\xBF"));
    ZEXPECT(wide == u"a\xDFFF");
    // A low surrogate before a high one is two lone surrogates.
    ZEXPECT(!to_utf16(wide, "\xED\xB0\x80\xED\xA0\x80"));
    ZEXPECT(wide == u"\xDC00\xD800");
}

ZEST_CASE(to_utf16_writes_into_any_buffer_of_units) {
    small_vector<char16_t, 4> wide;
    ZEXPECT(!to_utf16(wide, "a longer text"));
    ZEXPECT(std::u16string_view(wide.data(), wide.size()) == u"a longer text");
}

ZEST_CASE(to_utf16_of_what_is_no_wtf8_fails) {
    constexpr std::string_view invalid[] = {
        "\x80"sv,
        "\xFF"sv,
        "\xC3"sv,
        "a\xE4\xB8"sv,
        "\xE4\xB8x"sv,
        // Overlong forms of `/`, of NUL and of U+FFFF.
        "\xC0\xAF"sv,
        "\xE0\x80\x80"sv,
        "\xF0\x8F\xBF\xBF"sv,
        // Past U+10FFFF.
        "\xF4\x90\x80\x80"sv,
        // U+1F600 as a surrogate pair of three-byte halves.
        "\xED\xA0\xBD\xED\xB8\x80"sv,
    };
    for(std::size_t i = 0; i < std::size(invalid); ++i) {
        ZEST_CONTEXT("input {}", i);
        std::u16string wide;
        ZEXPECT(to_utf16(wide, invalid[i]) == std::errc::illegal_byte_sequence);
    }
    // What came before the error stays written.
    std::u16string wide;
    ZEXPECT(to_utf16(wide, "ab\xFF") == std::errc::illegal_byte_sequence);
    ZEXPECT(wide == u"ab");
}

ZEST_CASE(from_utf16_writes_wtf8) {
    ZEXPECT(narrow(u"Aé中\U0001F600") == "A\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80");
    ZEXPECT(narrow(u"\xD800") == "\xED\xA0\x80");
    // A high surrogate cut off at the end, and one before no low one.
    ZEXPECT(narrow(u"a\xD83D") == "a\xED\xA0\xBD");
    ZEXPECT(narrow(u"\xD83Dx") == "\xED\xA0\xBDx");
}

ZEST_CASE(from_utf16_writes_over_what_the_buffer_held) {
    small_string<4> out("some old text");
    from_utf16(out, u"\U0001F600");
    ZEXPECT(std::string_view(out) == "\xF0\x9F\x98\x80");
}

ZEST_CASE(utf16_roundtrip_keeps_lone_surrogates) {
    for(auto text:
        {u"plain"sv, u"\xD800"sv, u"\xDBFF\xDBFF"sv, u"\xDC00\xD800"sv, u"\U0010FFFF"sv}) {
        std::u16string back;
        ZASSERT(!to_utf16(back, narrow(text)));
        ZEXPECT(back == text);
    }
}

};  // ZEST_SUITE(support_utf16)

}  // namespace

}  // namespace kotatsu
