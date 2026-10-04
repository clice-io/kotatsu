#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kota/http/http.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

// Requests that fail before curl sees them: send() touches neither curl nor
// the network for them, so the url they name is never reached.

namespace kota::http {

namespace {

using namespace std::chrono_literals;

/// Where the requests would go if they got that far: a loopback port that
/// nothing serves.
constexpr std::string_view nowhere = "http://127.0.0.1:1/";

#if KOTA_HTTP_HAS_CODEC_JSON
/// A value the JSON codec refuses to encode.
struct Unencodable {};
#endif

}  // namespace

}  // namespace kota::http

#if KOTA_HTTP_HAS_CODEC_JSON
namespace kota::meta {

template <>
struct repr<http::Unencodable> {
    using type = std::string;

    template <typename Config>
    static bool serialize(auto&, const http::Unencodable&) {
        return codec::scoped_context<codec::rich_error>::fail(codec::rich_error("refused"));
    }
};

}  // namespace kota::meta
#endif

namespace kota::http {

namespace {

struct RequestFixture : zest::LoopFixture {
    http::client client;

    /// The error sending `built` fails with, if it fails.
    std::optional<error> failure_of(http::request built) {
        auto [sent] = run(std::move(built).send());
        if(!sent.has_error()) {
            return std::nullopt;
        }
        return std::move(sent).error();
    }
};

ZEST_SUITE(http_detail_request, RequestFixture) {

ZEST_CASE(empty_url_fails) {
    auto failed = failure_of(client.on(loop).get(""));
    ASSERT(failed.has_value());
    EXPECT(failed->kind == error_kind::invalid_request);
    EXPECT(failed->message() == "request url must not be empty");
}

ZEST_CASE(method_that_is_no_token_fails) {
    const std::vector<std::string> methods{"", "GET /", "GET\r\nX-Injected: 1", "G(ET"};
    for(std::size_t i = 0; i < methods.size(); ++i) {
        ZEST_CONTEXT("method {}", i);
        auto failed = failure_of(client.on(loop).request(methods[i], std::string(nowhere)));
        ASSERT(failed.has_value());
        EXPECT(failed->kind == error_kind::invalid_request);
        EXPECT(failed->message() == "request method must be an http token");
    }
}

ZEST_CASE(body_on_get_or_head_fails) {
    auto api = client.on(loop);
    for(auto method: {"GET", "HEAD", "get", "Head"}) {
        ZEST_CONTEXT("{}", method);
        auto failed = failure_of(api.request(method, std::string(nowhere)).body("unexpected"));
        ASSERT(failed.has_value());
        EXPECT(failed->kind == error_kind::invalid_request);
        EXPECT(failed->message() == "request body is not supported for GET or HEAD");
    }
}

// CR or LF would end the header line and start one of the value's making.
ZEST_CASE(header_that_would_break_its_line_fails) {
    const std::vector<header> broken{
        {"X-Test", "1\r\nX-Injected: 2"},
        {"X-Test", "1\nX-Injected: 2"},
        {"X-Test",
         std::string("1\0"
                     "2", 3)},
        {"", "empty name"},
        {"X Test", "space in the name"},
        {"X-Test:", "colon in the name"},
        {"X-Test\r\nX-Injected", "2"},
    };
    for(std::size_t i = 0; i < broken.size(); ++i) {
        ZEST_CONTEXT("header {}", i);
        auto failed = failure_of(
            client.on(loop).get(std::string(nowhere)).header(broken[i].name, broken[i].value));
        ASSERT(failed.has_value());
        EXPECT(failed->kind == error_kind::invalid_request);
        EXPECT(failed->message() ==
               "header names must be http tokens, and values must not hold CR, LF or NUL");
    }
}

ZEST_CASE(cookie_or_user_agent_that_would_break_its_line_fails) {
    auto api = client.on(loop);
    const std::vector<std::pair<std::string_view, http::request>> built{
        {"cookies",    api.get(std::string(nowhere)).cookies("a=1\r\nX-Injected: 2")     },
        {"user agent", api.get(std::string(nowhere)).user_agent("agent\r\nX-Injected: 2")},
    };
    for(const auto& [what, request]: built) {
        ZEST_CONTEXT("{}", what);
        auto failed = failure_of(request);
        ASSERT(failed.has_value());
        EXPECT(failed->kind == error_kind::invalid_request);
        EXPECT(failed->message() == "cookies and user agent must not hold CR, LF or NUL");
    }
}

ZEST_CASE(min_tls_above_max_tls_fails) {
    auto failed = failure_of(client.on(loop)
                                 .get(std::string(nowhere))
                                 .min_tls_version(tls_version::tls1_3)
                                 .max_tls_version(tls_version::tls1_2));
    ASSERT(failed.has_value());
    EXPECT(failed->kind == error_kind::invalid_request);
    EXPECT(failed->message() == "min tls version must not exceed max tls version");
}

ZEST_CASE(proxy_without_url_fails) {
    auto failed = failure_of(client.on(loop).get(std::string(nowhere)).proxy(""));
    ASSERT(failed.has_value());
    EXPECT(failed->kind == error_kind::invalid_request);
    EXPECT(failed->message() == "proxy url must not be empty");
}

ZEST_CASE(negative_timeout_fails) {
    auto failed = failure_of(client.on(loop).get(std::string(nowhere)).timeout(-1ms));
    ASSERT(failed.has_value());
    EXPECT(failed->kind == error_kind::invalid_request);
    EXPECT(failed->message() == "timeout must be non-negative");
}

// A timeout can exceed only a 32-bit long, which Windows has.
ZEST_CASE(timeout_beyond_a_long_fails, skip = sizeof(long) > 4) {
    auto failed =
        failure_of(client.on(loop).get(std::string(nowhere)).timeout(std::chrono::days(30)));
    ASSERT(failed.has_value());
    EXPECT(failed->kind == error_kind::invalid_request);
    EXPECT(failed->message() == "timeout exceeds libcurl timeout range");
}

#if KOTA_HTTP_HAS_CODEC_JSON
ZEST_CASE(json_that_cannot_be_encoded_fails) {
    auto failed = failure_of(client.on(loop).post(std::string(nowhere)).json(Unencodable{}));
    ASSERT(failed.has_value());
    EXPECT(failed->kind == error_kind::json_encode);
    EXPECT(zest::contains(failed->message(), "refused"));
}
#endif

// A send that took the url and body with it would fail the second time for
// the url instead.
ZEST_CASE(request_sent_by_reference_can_be_sent_again) {
    auto built = client.on(loop).get(std::string(nowhere)).body("unexpected");
    auto [first] = run(built.send());
    auto [second] = run(built.send());
    ASSERT(first.has_error());
    ASSERT(second.has_error());
    EXPECT(first.error().message() == "request body is not supported for GET or HEAD");
    EXPECT(second.error().message() == "request body is not supported for GET or HEAD");
}

};  // ZEST_SUITE(http_detail_request)

}  // namespace

}  // namespace kota::http
