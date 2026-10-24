#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#include "kota/zest/zest.h"
#include "kota/support/utf16.h"

namespace kota {

namespace {

using namespace std::literals;

ZEST_SUITE(support_utf16) {

ZEST_CASE(to_utf16_converts_code_points_of_every_length) {
    // U+0041, U+00E9, U+4E2D, U+1F600: one to four bytes, the last a pair.
    auto wide = to_utf16("A\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80");
    ZASSERT(wide.has_value());
    ZEXPECT(*wide == u"Aé中\U0001F600");
    ZEXPECT(to_utf16("").value() == u"");
    ZEXPECT(to_utf16("a\0b"sv).value() == u"a\0b"sv);
}

ZEST_CASE(to_utf16_reads_a_lone_surrogate_in_three_bytes) {
    ZEXPECT(to_utf16("\xED\xA0\x80").value() == u"\xD800");
    ZEXPECT(to_utf16("a\xED\xBF\xBF").value() == u"a\xDFFF");
    // A low surrogate before a high one is two lone surrogates.
    ZEXPECT(to_utf16("\xED\xB0\x80\xED\xA0\x80").value() == u"\xDC00\xD800");
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
        auto wide = to_utf16(invalid[i]);
        ZASSERT(!wide.has_value());
        ZEXPECT(wide.error() == std::errc::illegal_byte_sequence);
    }
}

ZEST_CASE(from_utf16_writes_wtf8) {
    ZEXPECT(from_utf16(u"Aé中\U0001F600") == "A\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80");
    ZEXPECT(from_utf16(u"\xD800") == "\xED\xA0\x80");
    // A high surrogate cut off at the end, and one before no low one.
    ZEXPECT(from_utf16(u"a\xD83D") == "a\xED\xA0\xBD");
    ZEXPECT(from_utf16(u"\xD83Dx") == "\xED\xA0\xBDx");
}

ZEST_CASE(utf16_roundtrip_keeps_lone_surrogates) {
    for(auto text:
        {u"plain"sv, u"\xD800"sv, u"\xDBFF\xDBFF"sv, u"\xDC00\xD800"sv, u"\U0010FFFF"sv}) {
        auto back = to_utf16(from_utf16(text));
        ZASSERT(back.has_value());
        ZEXPECT(*back == text);
    }
}

};  // ZEST_SUITE(support_utf16)

}  // namespace

}  // namespace kota
