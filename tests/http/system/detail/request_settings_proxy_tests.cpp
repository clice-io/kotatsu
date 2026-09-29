#include "async/harness/loop_fixture.h"
#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

// The test server plays the proxy: a request through it names the whole url
// it wants, which curl never looks up itself.

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_request_settings_proxy, test::LoopFixture) {

ZEST_CASE(requests_go_through_the_proxy) {
    test::HttpServer proxy(loop);
    ASSERT(proxy.listening());
    auto client = http::client().proxy(proxy.url(""));

    auto [reply] = run(client.on(loop).get("http://kotatsu.invalid/path?q=1").send());
    EXPECT(reply.has_value());

    ASSERT(proxy.requests().size() == 1U);
    EXPECT(proxy.requests()[0].target == "http://kotatsu.invalid/path?q=1");
    EXPECT(proxy.requests()[0].header("host") == "kotatsu.invalid");
    EXPECT(proxy.requests()[0].count("proxy-authorization") == 0U);
}

ZEST_CASE(proxy_credentials_are_sent) {
    test::HttpServer proxy(loop);
    ASSERT(proxy.listening());
    auto client = http::client().proxy(http::proxy{
        .url = proxy.url(""),
        .username = "user",
        .password = "secret",
    });

    auto [reply] = run(client.on(loop).get("http://kotatsu.invalid/").send());
    EXPECT(reply.has_value());

    ASSERT(proxy.requests().size() == 1U);
    EXPECT(proxy.requests()[0].header("proxy-authorization") == "Basic dXNlcjpzZWNyZXQ=");
}

ZEST_CASE(no_proxy_goes_straight_to_the_server) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    test::RefusingPort nowhere;
    ASSERT(nowhere.port > 0);
    auto client = http::client().proxy(nowhere.url());

    auto [reply] = run(client.on(loop).get(server.url("/direct")).no_proxy().send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].target == "/direct");
}

ZEST_CASE(proxy_replaces_a_no_proxy) {
    test::HttpServer proxy(loop);
    ASSERT(proxy.listening());
    auto client = http::client().no_proxy();

    auto [reply] = run(client.on(loop).get("http://kotatsu.invalid/").proxy(proxy.url("")).send());
    EXPECT(reply.has_value());

    ASSERT(proxy.requests().size() == 1U);
    EXPECT(proxy.requests()[0].target == "http://kotatsu.invalid/");
}

};  // ZEST_SUITE(http_detail_request_settings_proxy)

}  // namespace

}  // namespace kota::http
