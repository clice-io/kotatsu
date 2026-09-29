#include <cstring>
#include <string>
#include <string_view>

#include "async/harness/loop_fixture.h"
#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_request_settings_curl_option, test::LoopFixture) {

// The option is set only when the request starts: a C string's text is
// copied at once, before its buffer changes.
ZEST_CASE(string_option_given_as_a_c_string_is_copied) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    std::string agent = "original-agent";
    auto client = test::loopback_client().curl_option(CURLOPT_USERAGENT, agent.c_str());
    agent.replace(0, 8, "replaced");

    auto [reply] = run(client.on(loop).get(server.url("/")).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("user-agent") == "original-agent");
}

ZEST_CASE(string_option_given_as_a_string_is_copied) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();
    std::string_view view = "from-view";
    auto api = client.on(loop);

    auto [owned] = run(
        api.get(server.url("/")).curl_option(CURLOPT_USERAGENT, std::string("from-string")).send());
    auto [viewed] = run(api.get(server.url("/")).curl_option(CURLOPT_USERAGENT, view).send());
    EXPECT(owned.has_value());
    EXPECT(viewed.has_value());

    ASSERT(server.requests().size() == 2U);
    EXPECT(server.requests()[0].header("user-agent") == "from-string");
    EXPECT(server.requests()[1].header("user-agent") == "from-view");
}

// curl writes into the buffer: a copy would take the text in, not the error
// out.
ZEST_CASE(pointer_option_is_passed_as_it_is) {
    auto nowhere = test::refusing_url();
    ASSERT(!nowhere.empty());
    char message[CURL_ERROR_SIZE] = {};
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop)
                           .get(nowhere)
                           .curl_option(CURLOPT_ERRORBUFFER, static_cast<char*>(message))
                           .send());
    ASSERT(reply.has_error());
    EXPECT(std::strlen(message) > 0U);
}

ZEST_CASE(options_go_after_the_requests_own) {
    test::HttpServer server(loop, [](const test::Received&) {
        return test::Reply{.status = 302, .headers = {{"Location", "/elsewhere"}}};
    });
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop)
                           .get(server.url("/"))
                           .user_agent("setting")
                           .curl_option(CURLOPT_USERAGENT, "option")
                           .curl_option(CURLOPT_FOLLOWLOCATION, 0L)
                           .send());
    ASSERT(reply.has_value());
    EXPECT(reply->status == 302);

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("user-agent") == "option");
}

// kotatsu finds a transfer by CURLOPT_PRIVATE, which it sets after the
// caller's options.
ZEST_CASE(private_option_leaves_the_request_working) {
    test::HttpServer server(loop, [](const test::Received&) { return test::Reply{.body = "ok"}; });
    ASSERT(server.listening());
    auto client = test::loopback_client();
    int mine = 0;

    auto [reply] = run(client.on(loop)
                           .get(server.url("/"))
                           .curl_option(CURLOPT_PRIVATE, static_cast<void*>(&mine))
                           .send());
    ASSERT(reply.has_value());
    EXPECT(reply->text() == "ok");
}

ZEST_CASE(option_curl_refuses_fails) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] =
        run(client.on(loop).get(server.url("/")).curl_option(CURLOPT_SSLVERSION, 999L).send());
    ASSERT(reply.has_error());
    EXPECT(reply.error().kind == error_kind::curl);
    EXPECT(reply.error().curl_code == CURLE_BAD_FUNCTION_ARGUMENT);
    EXPECT(server.requests().empty());
}

};  // ZEST_SUITE(http_detail_request_settings_curl_option)

}  // namespace

}  // namespace kota::http
