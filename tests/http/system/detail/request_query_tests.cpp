#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_request_query, zest::LoopFixture) {

ZEST_CASE(query_parameters_are_percent_encoded_in_order) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop)
                           .get(server.url("/inspect"))
                           .query("q", "a b")
                           .query("path", "x/y")
                           .query("q", "again")
                           .send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].target == "/inspect?q=a%20b&path=x%2Fy&q=again");
}

ZEST_CASE(query_extends_a_url_that_has_one) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/p?x=1")).query("y", "2").send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].target == "/p?x=1&y=2");
}

ZEST_CASE(response_url_carries_the_query) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/p")).query("y", "2").send());
    ASSERT(reply.has_value());
    EXPECT(reply->url == server.url("/p?y=2"));
}

};  // ZEST_SUITE(http_detail_request_query)

}  // namespace

}  // namespace kota::http
