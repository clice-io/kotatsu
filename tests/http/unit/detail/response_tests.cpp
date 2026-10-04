#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "kota/http/detail/response.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"

namespace kota::http {

namespace {

/// A response whose body is `text`.
response with_body(std::string_view text) {
    response out;
    for(char c: text) {
        out.body.push_back(static_cast<std::byte>(c));
    }
    return out;
}

ZEST_SUITE(http_detail_response) {

ZEST_CASE(ok_holds_for_2xx_only) {
    for(int status: {100, 199, 200, 204, 299, 300, 404, 500}) {
        ZEST_CONTEXT("status {}", status);
        response out;
        out.status = status;
        ZEXPECT(out.ok() == (status >= 200 && status < 300));
    }
}

ZEST_CASE(text_and_bytes_view_the_whole_body) {
    constexpr std::string_view body("ok\0!", 4);
    auto out = with_body(body);
    ZEXPECT(out.bytes().size() == 4U);
    ZEXPECT((out.bytes().data() == out.body.data()));
    ZEXPECT(out.text() == body);
    ZEXPECT((static_cast<const void*>(out.text().data()) == out.body.data()));
}

ZEST_CASE(empty_body_has_empty_text) {
    response out;
    ZEXPECT(out.bytes().empty());
    ZEXPECT(out.text().empty());
    ZEXPECT(out.text_copy().empty());
}

ZEST_CASE(text_copy_owns_its_text) {
    auto out = with_body("body");
    auto copy = out.text_copy();
    out.body.clear();
    ZEXPECT(copy == "body");
}

ZEST_CASE(header_value_ignores_case_and_takes_the_first) {
    response out;
    out.headers = {
        {"Content-Type", "text/plain"},
        {"Set-Cookie",   "a=1"       },
        {"set-cookie",   "b=2"       },
    };

    auto type = out.header_value("content-type");
    ZASSERT(type.has_value());
    ZEXPECT(*type == "text/plain");
    auto cookie = out.header_value("SET-COOKIE");
    ZASSERT(cookie.has_value());
    ZEXPECT(*cookie == "a=1");
}

ZEST_CASE(header_value_of_a_missing_header_is_empty) {
    response out;
    out.headers = {
        {"Content-Type", "text/plain"}
    };
    ZEXPECT(!out.header_value("content-length").has_value());
}

};  // ZEST_SUITE(http_detail_response)

}  // namespace

}  // namespace kota::http
