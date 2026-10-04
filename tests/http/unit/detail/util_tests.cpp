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
    ZEXPECT(iequals("Content-Type", "content-TYPE"));
    ZEXPECT(iequals("", ""));
    ZEXPECT(!iequals("Content-Type", "Content-Typ"));
    ZEXPECT(!iequals("a-b", "a_b"));
    // É and é differ past ASCII, where nothing folds case.
    ZEXPECT(!iequals("\xC3\x89", "\xC3\xA9"));
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
    ZEXPECT(headers == expected);
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
    ZEXPECT(headers == expected);
}

ZEST_CASE(insert_header_keeps_a_header_of_any_case) {
    std::vector<header> headers{
        {"Accept", "text/plain"}
    };
    insert_header(headers, "accept", "*/*");

    const std::vector<header> expected{
        {"Accept", "text/plain"}
    };
    ZEXPECT(headers == expected);
}

ZEST_CASE(insert_header_appends_a_new_header) {
    std::vector<header> headers;
    insert_header(headers, "Accept", "*/*");

    const std::vector<header> expected{
        {"Accept", "*/*"}
    };
    ZEXPECT(headers == expected);
}

ZEST_CASE(trim_ascii_drops_whitespace_at_both_ends) {
    ZEXPECT(trim_ascii(" \t value \r\n") == "value");
    ZEXPECT(trim_ascii("inner space") == "inner space");
    ZEXPECT(trim_ascii(" \t\r\n").empty());
    ZEXPECT(trim_ascii("").empty());
}

ZEST_CASE(percent_encode_keeps_unreserved_characters) {
    constexpr std::string_view unreserved =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    ZEXPECT(percent_encode(unreserved) == unreserved);
}

ZEST_CASE(percent_encode_writes_other_bytes_in_upper_case_hex) {
    ZEXPECT(percent_encode("a b") == "a%20b");
    ZEXPECT(percent_encode("x/y?z=1&w+v") == "x%2Fy%3Fz%3D1%26w%2Bv");
    ZEXPECT(percent_encode("\xC3\xA9") == "%C3%A9");
    ZEXPECT(percent_encode(std::string_view("\0\x7F\xFF", 3)) == "%00%7F%FF");
}

ZEST_CASE(encode_pairs_joins_encoded_pairs_with_ampersands) {
    ZEXPECT(encode_pairs({}).empty());
    ZEXPECT(encode_pairs({
                {"q", "a b"}
    }) == "q=a%20b");
    ZEXPECT(encode_pairs({
                {"name", "alice"},
                {"a&b",  "c=d"  },
                {"",     ""     }
    }) == "name=alice&a%26b=c%3Dd&=");
}

ZEST_CASE(base64_encode_matches_rfc_4648) {
    ZEXPECT(base64_encode("").empty());
    ZEXPECT(base64_encode("f") == "Zg==");
    ZEXPECT(base64_encode("fo") == "Zm8=");
    ZEXPECT(base64_encode("foo") == "Zm9v");
    ZEXPECT(base64_encode("foob") == "Zm9vYg==");
    ZEXPECT(base64_encode("fooba") == "Zm9vYmE=");
    ZEXPECT(base64_encode("foobar") == "Zm9vYmFy");
}

ZEST_CASE(base64_encode_takes_any_byte) {
    ZEXPECT(base64_encode(std::string_view("\0\xFF\x80\x7F", 4)) == "AP+Afw==");
}

};  // ZEST_SUITE(http_detail_util)

}  // namespace

}  // namespace kota::http::detail
