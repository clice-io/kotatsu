#include <string>
#include <string_view>
#include <vector>

#include "kota/http/detail/util.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"

namespace kota::http::detail {

namespace {

ZEST_SUITE(http_detail_util) {

ZEST_CASE(iequals_ignores_the_case_of_ascii_letters) {
    EXPECT(iequals("Content-Type", "content-TYPE"));
    EXPECT(iequals("", ""));
    EXPECT(!iequals("Content-Type", "Content-Typ"));
    EXPECT(!iequals("a-b", "a_b"));
    // É and é differ past ASCII, where nothing folds case.
    EXPECT(!iequals("\xC3\x89", "\xC3\xA9"));
}

ZEST_CASE(upsert_header_replaces_a_header_of_any_case) {
    std::vector<header> headers{
        {"Accept", "text/plain"},
        {"X-Test", "one"       },
        {"x-test", "two"       },
    };
    upsert_header(headers, "x-TEST", "three");

    const std::vector<header> expected{
        {"Accept", "text/plain"},
        {"x-TEST", "three"     },
        {"x-test", "two"       },
    };
    EXPECT(headers == expected);
}

ZEST_CASE(upsert_header_appends_a_new_header) {
    std::vector<header> headers{
        {"Accept", "text/plain"}
    };
    upsert_header(headers, "X-Test", "one");

    const std::vector<header> expected{
        {"Accept", "text/plain"},
        {"X-Test", "one"       },
    };
    EXPECT(headers == expected);
}

ZEST_CASE(insert_header_keeps_a_header_of_any_case) {
    std::vector<header> headers{
        {"Accept", "text/plain"}
    };
    insert_header(headers, "accept", "*/*");

    const std::vector<header> expected{
        {"Accept", "text/plain"}
    };
    EXPECT(headers == expected);
}

ZEST_CASE(insert_header_appends_a_new_header) {
    std::vector<header> headers;
    insert_header(headers, "Accept", "*/*");

    const std::vector<header> expected{
        {"Accept", "*/*"}
    };
    EXPECT(headers == expected);
}

ZEST_CASE(trim_ascii_drops_whitespace_at_both_ends) {
    EXPECT(trim_ascii(" \t value \r\n") == "value");
    EXPECT(trim_ascii("inner space") == "inner space");
    EXPECT(trim_ascii(" \t\r\n").empty());
    EXPECT(trim_ascii("").empty());
}

ZEST_CASE(percent_encode_keeps_unreserved_characters) {
    constexpr std::string_view unreserved =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    EXPECT(percent_encode(unreserved) == unreserved);
}

ZEST_CASE(percent_encode_writes_other_bytes_in_upper_case_hex) {
    EXPECT(percent_encode("a b") == "a%20b");
    EXPECT(percent_encode("x/y?z=1&w+v") == "x%2Fy%3Fz%3D1%26w%2Bv");
    EXPECT(percent_encode("\xC3\xA9") == "%C3%A9");
    EXPECT(percent_encode(std::string_view("\0\x7F\xFF", 3)) == "%00%7F%FF");
}

ZEST_CASE(encode_pairs_joins_encoded_pairs_with_ampersands) {
    EXPECT(encode_pairs({}).empty());
    EXPECT(encode_pairs({
               {"q", "a b"}
    }) == "q=a%20b");
    EXPECT(encode_pairs({
               {"name", "alice"},
               {"a&b",  "c=d"  },
               {"",     ""     }
    }) == "name=alice&a%26b=c%3Dd&=");
}

ZEST_CASE(base64_encode_matches_rfc_4648) {
    EXPECT(base64_encode("").empty());
    EXPECT(base64_encode("f") == "Zg==");
    EXPECT(base64_encode("fo") == "Zm8=");
    EXPECT(base64_encode("foo") == "Zm9v");
    EXPECT(base64_encode("foob") == "Zm9vYg==");
    EXPECT(base64_encode("fooba") == "Zm9vYmE=");
    EXPECT(base64_encode("foobar") == "Zm9vYmFy");
}

ZEST_CASE(base64_encode_takes_any_byte) {
    EXPECT(base64_encode(std::string_view("\0\xFF\x80\x7F", 4)) == "AP+Afw==");
}

};  // ZEST_SUITE(http_detail_util)

}  // namespace

}  // namespace kota::http::detail
