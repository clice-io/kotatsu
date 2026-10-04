#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_request_auth, zest::LoopFixture) {

ZEST_CASE(bearer_auth_sends_the_token) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/")).bearer_auth("t0ken").send());
    ZEXPECT(reply.has_value());

    ZASSERT(server.requests().size() == 1U);
    ZEXPECT(server.requests()[0].header("authorization") == "Bearer t0ken");
}

ZEST_CASE(basic_auth_sends_the_credentials_in_base64) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/")).basic_auth("user", "p:ss").send());
    ZEXPECT(reply.has_value());

    ZASSERT(server.requests().size() == 1U);
    ZEXPECT(server.requests()[0].header("authorization") == "Basic dXNlcjpwOnNz");
}

ZEST_CASE(later_auth_replaces_an_earlier_one) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());
    auto client = test::loopback_client().header("Authorization", "Bearer client");

    auto [reply] = run(client.on(loop)
                           .get(server.url("/"))
                           .bearer_auth("request")
                           .basic_auth("user", "p:ss")
                           .send());
    ZEXPECT(reply.has_value());

    ZASSERT(server.requests().size() == 1U);
    ZEXPECT(server.requests()[0].count("authorization") == 1U);
    ZEXPECT(server.requests()[0].header("authorization") == "Basic dXNlcjpwOnNz");
}

};  // ZEST_SUITE(http_detail_request_auth)

}  // namespace

}  // namespace kota::http
