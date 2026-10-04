#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_request_method, zest::LoopFixture) {

ZEST_CASE(builders_send_their_methods) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());
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
        ZEXPECT(reply.has_value());
    }

    ZASSERT(server.requests().size() == built.size());
    for(std::size_t i = 0; i < built.size(); ++i) {
        ZEST_CONTEXT("{}", built[i].first);
        ZEXPECT(server.requests()[i].method == built[i].first);
    }
}

ZEST_CASE(other_methods_go_as_written) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);

    for(std::string method: {"OPTIONS", "PROPFIND", "put"}) {
        ZEST_CONTEXT("{}", method);
        auto [reply] = run(api.request(method, server.url("/caps")).send());
        ZEXPECT(reply.has_value());
    }

    ZASSERT(server.requests().size() == 3U);
    ZEXPECT(server.requests()[0].method == "OPTIONS");
    ZEXPECT(server.requests()[1].method == "PROPFIND");
    ZEXPECT(server.requests()[2].method == "put");
    ZEXPECT(server.requests()[0].target == "/caps");
}

ZEST_CASE(get_head_and_post_match_any_case) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);

    for(std::string method: {"get", "Head", "pOST"}) {
        ZEST_CONTEXT("{}", method);
        auto [reply] = run(api.request(method, server.url("/")).send());
        ZEXPECT(reply.has_value());
    }

    ZASSERT(server.requests().size() == 3U);
    ZEXPECT(server.requests()[0].method == "GET");
    ZEXPECT(server.requests()[1].method == "HEAD");
    ZEXPECT(server.requests()[2].method == "POST");
}

ZEST_CASE(method_replaces_the_builders) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/")).method("PUT").body("data").send());
    ZEXPECT(reply.has_value());

    ZASSERT(server.requests().size() == 1U);
    ZEXPECT(server.requests()[0].method == "PUT");
    ZEXPECT(server.requests()[0].body == "data");
}

ZEST_CASE(head_reply_has_headers_and_no_body) {
    test::HttpServer server(loop, [](const test::Received&) {
        return test::Reply{
            .headers = {{"X-Mode", "head"}},
            .body = "not sent",
        };
    });
    ZASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).head(server.url("/only-headers")).send());
    ZASSERT(reply.has_value());
    ZEXPECT(reply->status == 200);
    ZEXPECT(reply->body.empty());
    ZEXPECT(reply->header_value("x-mode") == "head");
    ZEXPECT(reply->header_value("content-length") == "8");
}

};  // ZEST_SUITE(http_detail_request_method)

}  // namespace

}  // namespace kota::http
