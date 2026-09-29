#include <chrono>

#include "async/harness/loop_fixture.h"
#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

using namespace std::chrono_literals;

ZEST_SUITE(http_detail_request_settings_timeout, test::LoopFixture) {

// The subject is curl's timer: the server holds its reply for good.
ZEST_CASE(request_past_its_timeout_fails) {
    event never;
    test::HttpServer server(loop,
                            [&](const test::Received&) { return test::Reply{.hold = &never}; });
    ASSERT(server.listening());
    auto client = http::client().timeout(50ms);

    auto [reply] = run(client.on(loop).get(server.url("/")).send());
    ASSERT(reply.has_error());
    EXPECT(reply.error().kind == error_kind::curl);
    EXPECT(reply.error().curl_code == CURLE_OPERATION_TIMEDOUT);
}

ZEST_CASE(request_timeout_replaces_the_clients) {
    event never;
    test::HttpServer server(loop, [&](const test::Received& request) {
        return request.target == "/slow" ? test::Reply{.hold = &never} : test::Reply{};
    });
    ASSERT(server.listening());
    auto client = http::client().timeout(1h);

    auto [reply] = run(client.on(loop).get(server.url("/slow")).timeout(50ms).send());
    ASSERT(reply.has_error());
    EXPECT(reply.error().curl_code == CURLE_OPERATION_TIMEDOUT);
}

};  // ZEST_SUITE(http_detail_request_settings_timeout)

}  // namespace

}  // namespace kota::http
