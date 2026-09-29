#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_request_method, test::LoopFixture) {

ZEST_CASE(builders_send_their_methods) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);
    auto url = server.url("/");
    std::vector<std::pair<std::string_view, http::request>> built{
        {"GET",    api.get(url)  },
        {"POST",   api.post(url) },
        {"PUT",    api.put(url)  },
        {"PATCH",  api.patch(url)},
        {"DELETE", api.del(url)  },
        {"HEAD",   api.head(url) },
    };

    for(auto& [method, request]: built) {
        ZEST_CONTEXT("{}", method);
        auto [reply] = run(request.send());
        EXPECT(reply.has_value());
    }

    ASSERT(server.requests().size() == built.size());
    for(std::size_t i = 0; i < built.size(); ++i) {
        ZEST_CONTEXT("{}", built[i].first);
        EXPECT(server.requests()[i].method == built[i].first);
    }
}

ZEST_CASE(other_methods_go_as_written) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);

    for(std::string method: {"OPTIONS", "PROPFIND", "put"}) {
        ZEST_CONTEXT("{}", method);
        auto [reply] = run(api.request(method, server.url("/caps")).send());
        EXPECT(reply.has_value());
    }

    ASSERT(server.requests().size() == 3U);
    EXPECT(server.requests()[0].method == "OPTIONS");
    EXPECT(server.requests()[1].method == "PROPFIND");
    EXPECT(server.requests()[2].method == "put");
    EXPECT(server.requests()[0].target == "/caps");
}

ZEST_CASE(get_head_and_post_match_any_case) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);

    for(std::string method: {"get", "Head", "pOST"}) {
        ZEST_CONTEXT("{}", method);
        auto [reply] = run(api.request(method, server.url("/")).send());
        EXPECT(reply.has_value());
    }

    ASSERT(server.requests().size() == 3U);
    EXPECT(server.requests()[0].method == "GET");
    EXPECT(server.requests()[1].method == "HEAD");
    EXPECT(server.requests()[2].method == "POST");
}

ZEST_CASE(method_replaces_the_builders) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/")).method("PUT").body("data").send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].method == "PUT");
    EXPECT(server.requests()[0].body == "data");
}

ZEST_CASE(head_reply_has_headers_and_no_body) {
    test::HttpServer server(loop, [](const test::Received&) {
        return test::Reply{
            .headers = {{"X-Mode", "head"}},
            .body = "not sent",
        };
    });
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).head(server.url("/only-headers")).send());
    ASSERT(reply.has_value());
    EXPECT(reply->status == 200);
    EXPECT(reply->body.empty());
    EXPECT(reply->header_value("x-mode") == "head");
    EXPECT(reply->header_value("content-length") == "8");
}

};  // ZEST_SUITE(http_detail_request_method)

}  // namespace

}  // namespace kota::http
