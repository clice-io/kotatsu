#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

// The test server plays the proxy: a request through it names the whole url
// it wants, which curl never looks up itself.

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_request_settings_proxy, zest::LoopFixture) {

ZEST_CASE(requests_go_through_the_proxy) {
    test::HttpServer proxy(loop);
    ZASSERT(proxy.listening());
    auto client = test::loopback_client().proxy(proxy.url(""));

    auto [reply] = run(client.on(loop).get("http://kotatsu.invalid/path?q=1").send());
    ZEXPECT(reply.has_value());

    ZASSERT(proxy.requests().size() == 1U);
    ZEXPECT(proxy.requests()[0].target == "http://kotatsu.invalid/path?q=1");
    ZEXPECT(proxy.requests()[0].header("host") == "kotatsu.invalid");
    ZEXPECT(proxy.requests()[0].count("proxy-authorization") == 0U);
}

ZEST_CASE(proxy_credentials_are_sent) {
    test::HttpServer proxy(loop);
    ZASSERT(proxy.listening());
    auto client = test::loopback_client().proxy(http::proxy{
        .url = proxy.url(""),
        .username = "user",
        .password = "secret",
    });

    auto [reply] = run(client.on(loop).get("http://kotatsu.invalid/").send());
    ZEXPECT(reply.has_value());

    ZASSERT(proxy.requests().size() == 1U);
    ZEXPECT(proxy.requests()[0].header("proxy-authorization") == "Basic dXNlcjpzZWNyZXQ=");
}

ZEST_CASE(no_proxy_goes_straight_to_the_server) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());
    auto nowhere = test::refusing_url();
    ZASSERT(!nowhere.empty());
    auto client = test::loopback_client().proxy(nowhere);

    auto [reply] = run(client.on(loop).get(server.url("/direct")).no_proxy().send());
    ZEXPECT(reply.has_value());

    ZASSERT(server.requests().size() == 1U);
    ZEXPECT(server.requests()[0].target == "/direct");
}

ZEST_CASE(proxy_replaces_a_no_proxy) {
    test::HttpServer proxy(loop);
    ZASSERT(proxy.listening());
    auto client = test::loopback_client().no_proxy();

    auto [reply] = run(client.on(loop).get("http://kotatsu.invalid/").proxy(proxy.url("")).send());
    ZEXPECT(reply.has_value());

    ZASSERT(proxy.requests().size() == 1U);
    ZEXPECT(proxy.requests()[0].target == "http://kotatsu.invalid/");
}

};  // ZEST_SUITE(http_detail_request_settings_proxy)

}  // namespace

}  // namespace kota::http
