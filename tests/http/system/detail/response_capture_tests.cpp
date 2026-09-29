#include <string>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_response_capture, test::LoopFixture) {

ZEST_CASE(error_status_is_a_response) {
    test::HttpServer server(loop, [](const test::Received&) {
        return test::Reply{.status = 404, .body = "missing"};
    });
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/gone")).send());
    ASSERT(reply.has_value());
    EXPECT(reply->status == 404);
    EXPECT(!reply->ok());
    EXPECT(reply->text() == "missing");
    EXPECT(reply->url == server.url("/gone"));
}

ZEST_CASE(headers_are_kept_in_order_with_duplicates) {
    test::HttpServer server(loop, [](const test::Received&) {
        return test::Reply{
            .headers = {{"X-A", "1"}, {"X-B", " spaced\t"}, {"x-a", "3"}}
        };
    });
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/")).send());
    ASSERT(reply.has_value());
    std::vector<header> ours;
    for(const auto& item: reply->headers) {
        if(item.name.starts_with("X-") || item.name.starts_with("x-")) {
            ours.push_back(item);
        }
    }
    const std::vector<header> expected{
        {"X-A", "1"     },
        {"X-B", "spaced"},
        {"x-a", "3"     },
    };
    EXPECT(ours == expected);
    EXPECT(reply->header_value("x-a") == "1");
}

ZEST_CASE(body_keeps_every_byte) {
    std::string bytes;
    for(int i = 0; i < 256; ++i) {
        bytes.push_back(static_cast<char>(i));
    }
    test::HttpServer server(loop,
                            [&](const test::Received&) { return test::Reply{.body = bytes}; });
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/")).send());
    ASSERT(reply.has_value());
    EXPECT(reply->bytes().size() == bytes.size());
    EXPECT(reply->text() == bytes);
}

ZEST_CASE(large_body_arrives_whole) {
    std::string large(4 << 20, 'x');
    large.back() = 'y';
    test::HttpServer server(loop,
                            [&](const test::Received&) { return test::Reply{.body = large}; });
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/")).send());
    ASSERT(reply.has_value());
    EXPECT(reply->body.size() == large.size());
    // Compared as a plain bool, so that a failure does not print 4 MiB.
    EXPECT((reply->text() == large));
}

};  // ZEST_SUITE(http_detail_response_capture)

}  // namespace

}  // namespace kota::http
