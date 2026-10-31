#include <chrono>

#include "http/harness/server.h"
#include "kotatsu/http/http.h"
#include "kotatsu/zest/async.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/async/async.h"

namespace kotatsu::http {

namespace {

using namespace std::chrono_literals;

ZEST_SUITE(http_detail_request_settings_timeout, zest::LoopFixture) {

// The subject is curl's timer: the server holds its reply for good.
ZEST_CASE(request_past_its_timeout_fails) {
    async::event never;
    test::HttpServer server(loop,
                            [&](const test::Received&) { return test::Reply{.hold = &never}; });
    ZASSERT(server.listening());
    auto client = test::loopback_client().timeout(50ms);

    auto [reply] = run(client.on(loop).get(server.url("/")).send());
    ZASSERT(reply.has_error());
    ZEXPECT(reply.error().kind == error_kind::curl);
    ZEXPECT(reply.error().curl_code == CURLE_OPERATION_TIMEDOUT);
}

ZEST_CASE(request_timeout_shorter_than_the_clients_fails) {
    async::event never;
    test::HttpServer server(loop, [&](const test::Received& request) {
        return request.target == "/slow" ? test::Reply{.hold = &never} : test::Reply{};
    });
    ZASSERT(server.listening());
    auto client = test::loopback_client().timeout(1h);

    auto [reply] = run(client.on(loop).get(server.url("/slow")).timeout(50ms).send());
    ZASSERT(reply.has_error());
    ZEXPECT(reply.error().curl_code == CURLE_OPERATION_TIMEDOUT);
}

};  // ZEST_SUITE(http_detail_request_settings_timeout)

}  // namespace

}  // namespace kotatsu::http
