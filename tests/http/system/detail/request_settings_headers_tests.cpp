#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_request_settings_headers, zest::LoopFixture) {

ZEST_CASE(client_headers_reach_every_request) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());
    auto client = test::loopback_client().header("X-Client", "yes").user_agent("kotatsu-test/1");
    auto api = client.on(loop);

    auto [first] = run(api.get(server.url("/1")).send());
    auto [second] = run(api.post(server.url("/2")).send());
    ZEXPECT(first.has_value());
    ZEXPECT(second.has_value());

    ZASSERT(server.requests().size() == 2U);
    for(const auto& sent: server.requests()) {
        ZEST_CONTEXT("{}", sent.target);
        ZEXPECT(sent.header("x-client") == "yes");
        ZEXPECT(sent.header("user-agent") == "kotatsu-test/1");
    }
}

ZEST_CASE(header_replaces_one_of_any_case) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());
    auto client = test::loopback_client().header("X-Test", "client");

    auto [reply] = run(client.on(loop)
                           .get(server.url("/"))
                           .header("x-test", "request")
                           .header("X-TEST", "last")
                           .send());
    ZEXPECT(reply.has_value());

    ZASSERT(server.requests().size() == 1U);
    ZEXPECT(server.requests()[0].count("x-test") == 1U);
    ZEXPECT(server.requests()[0].header("x-test") == "last");
}

ZEST_CASE(default_header_keeps_one_already_set) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());
    auto client = test::loopback_client().header("Accept", "text/plain");

    auto [reply] = run(client.on(loop)
                           .get(server.url("/"))
                           .default_header("accept", "*/*")
                           .default_header("X-New", "added")
                           .send());
    ZEXPECT(reply.has_value());

    ZASSERT(server.requests().size() == 1U);
    ZEXPECT(server.requests()[0].count("accept") == 1U);
    ZEXPECT(server.requests()[0].header("accept") == "text/plain");
    ZEXPECT(server.requests()[0].header("x-new") == "added");
}

// curl takes "Name:" to mean that it should leave out a header of its own.
ZEST_CASE(header_with_an_empty_value_is_sent) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/")).header("X-Empty", "").send());
    ZEXPECT(reply.has_value());

    ZASSERT(server.requests().size() == 1U);
    ZEXPECT(server.requests()[0].count("x-empty") == 1U);
    ZEXPECT(server.requests()[0].header("x-empty") == "");
}

};  // ZEST_SUITE(http_detail_request_settings_headers)

}  // namespace

}  // namespace kota::http
