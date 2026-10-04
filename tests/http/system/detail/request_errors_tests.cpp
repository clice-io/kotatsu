#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

// How a request that curl cannot finish ends: with error_kind::curl and the
// code curl gives.

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_request_errors, zest::LoopFixture) {

ZEST_CASE(refused_connection_fails) {
    auto nowhere = test::refusing_url();
    ZASSERT(!nowhere.empty());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(nowhere).send());
    ZASSERT(reply.has_error());
    ZEXPECT(reply.error().kind == error_kind::curl);
    ZEXPECT(reply.error().curl_code == CURLE_COULDNT_CONNECT);
}

ZEST_CASE(connection_closed_without_a_reply_fails) {
    test::HttpServer server(loop, [](const test::Received&) { return test::Reply{.raw = ""}; });
    ZASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/")).send());
    ZASSERT(reply.has_error());
    ZEXPECT(reply.error().kind == error_kind::curl);
    ZEXPECT(reply.error().curl_code == CURLE_GOT_NOTHING);
}

ZEST_CASE(reply_that_is_not_http_fails) {
    test::HttpServer server(loop, [](const test::Received&) {
        return test::Reply{.raw = "not http at all\r\n\r\n"};
    });
    ZASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/")).send());
    ZASSERT(reply.has_error());
    ZEXPECT(reply.error().kind == error_kind::curl);
    ZEXPECT(reply.error().curl_code == CURLE_UNSUPPORTED_PROTOCOL);
}

ZEST_CASE(body_cut_short_fails) {
    test::HttpServer server(loop, [](const test::Received&) {
        return test::Reply{
            .headers = {{"Content-Length", "10"}},
            .body = "short",
        };
    });
    ZASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/")).send());
    ZASSERT(reply.has_error());
    ZEXPECT(reply.error().kind == error_kind::curl);
    ZEXPECT(reply.error().curl_code == CURLE_PARTIAL_FILE);
}

};  // ZEST_SUITE(http_detail_request_errors)

}  // namespace

}  // namespace kota::http
