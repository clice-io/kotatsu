#include <cstddef>
#include <string>
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

ZEST_SUITE(http_detail_request_body, test::LoopFixture) {

// Without a body of its own, curl would read one from the process's stdin.
ZEST_CASE(post_without_a_body_sends_an_empty_one) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    http::client client;

    auto [reply] = run(client.on(loop).post(server.url("/")).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    const auto& sent = server.requests()[0];
    EXPECT(sent.method == "POST");
    EXPECT(sent.header("content-length") == "0");
    EXPECT(!sent.chunked);
    EXPECT(sent.body.empty());
}

ZEST_CASE(empty_form_sends_an_empty_body) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    http::client client;

    auto [reply] = run(client.on(loop).post(server.url("/")).form({}).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("content-length") == "0");
    EXPECT(server.requests()[0].body.empty());
}

// A curl_option() may ask for an upload that has no reader of its own: it
// reads nothing, rather than the process's stdin.
ZEST_CASE(upload_without_a_reader_sends_nothing) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    http::client client;

    auto [reply] = run(client.on(loop).put(server.url("/")).curl_option(CURLOPT_UPLOAD, 1L).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].chunked);
    EXPECT(server.requests()[0].body.empty());
}

ZEST_CASE(body_goes_with_its_length) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    http::client client;

    auto [reply] = run(client.on(loop).post(server.url("/")).body("hello").send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("content-length") == "5");
    EXPECT(server.requests()[0].body == "hello");
}

ZEST_CASE(put_patch_and_delete_send_their_bodies) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    http::client client;
    auto api = client.on(loop);
    auto url = server.url("/");

    const std::vector<std::pair<std::string, std::string>> sends{
        {"PUT",    "put"   },
        {"PATCH",  "patch" },
        {"DELETE", "delete"},
    };
    for(const auto& [method, body]: sends) {
        ZEST_CONTEXT("{}", method);
        auto [reply] = run(api.request(method, url).body(body).send());
        EXPECT(reply.has_value());
    }

    ASSERT(server.requests().size() == 3U);
    EXPECT(server.requests()[0].method == "PUT");
    EXPECT(server.requests()[0].body == "put");
    EXPECT(server.requests()[1].method == "PATCH");
    EXPECT(server.requests()[1].body == "patch");
    EXPECT(server.requests()[2].method == "DELETE");
    EXPECT(server.requests()[2].body == "delete");
}

ZEST_CASE(body_keeps_every_byte) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    http::client client;
    std::string bytes;
    for(int i = 0; i < 256; ++i) {
        bytes.push_back(static_cast<char>(i));
    }

    auto [reply] = run(client.on(loop).post(server.url("/")).body(bytes).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].body == bytes);
}

// Past 1 MiB curl asks whether to go on (Expect: 100-continue) before it
// sends the body.
ZEST_CASE(large_body_arrives_whole) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    http::client client;
    std::string large(3 << 20, 'x');
    large.back() = 'y';

    auto [reply] = run(client.on(loop).post(server.url("/")).body(large).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].body.size() == large.size());
    EXPECT((server.requests()[0].body == large));
}

ZEST_CASE(json_text_is_sent_as_json) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    http::client client;

    auto [reply] = run(client.on(loop).post(server.url("/")).json_text(R"({"a":1})").send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("content-type") == "application/json");
    EXPECT(server.requests()[0].body == R"({"a":1})");
}

#if KOTA_HTTP_HAS_CODEC_JSON
ZEST_CASE(json_sends_its_value_encoded) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    http::client client;

    auto [reply] =
        run(client.on(loop).post(server.url("/")).json(std::vector<int>{1, 2, 3}).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("content-type") == "application/json");
    EXPECT(server.requests()[0].body == "[1,2,3]");
}
#endif

ZEST_CASE(form_is_sent_percent_encoded) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    http::client client;

    auto [reply] = run(client.on(loop)
                           .post(server.url("/"))
                           .form({
                               {"name", "alice"},
                               {"note", "a b+c"}
    })
                           .send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("content-type") == "application/x-www-form-urlencoded");
    EXPECT(server.requests()[0].body == "name=alice&note=a%20b%2Bc");
}

};  // ZEST_SUITE(http_detail_request_body)

}  // namespace

}  // namespace kota::http
