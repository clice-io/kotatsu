#include <format>
#include <string>

#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

/// Redirects /hop/N to /hop/N-1 and /hop/0 to /final, which answers
/// "final"; `server` is the server the handler belongs to.
test::HttpServer::Handler hops(const test::HttpServer& server) {
    return [&server](const test::Received& request) {
        if(request.target.starts_with("/hop/")) {
            auto left = std::stoi(request.target.substr(5));
            auto next =
                left == 0 ? server.url("/final") : server.url(std::format("/hop/{}", left - 1));
            return test::Reply{
                .status = 302,
                .headers = {{"Location", next}, {"X-Hop", "yes"}},
                .body = "redirect",
            };
        }
        return test::Reply{.body = "final"};
    };
}

struct RedirectFixture : zest::LoopFixture {
    test::HttpServer server{loop, hops(server)};
};

ZEST_SUITE(http_detail_request_settings_redirect, RedirectFixture) {

ZEST_CASE(redirects_are_followed_by_default) {
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/hop/1")).send());
    ASSERT(reply.has_value());
    EXPECT(reply->status == 200);
    EXPECT(reply->text() == "final");
    EXPECT(reply->url == server.url("/final"));
    // The headers are the last reply's.
    EXPECT(!reply->header_value("location").has_value());
    EXPECT(!reply->header_value("x-hop").has_value());
    EXPECT(server.requests().size() == 3U);
}

ZEST_CASE(redirect_policy_none_returns_the_redirect) {
    ASSERT(server.listening());
    auto client = test::loopback_client().redirect(redirect_policy::none());

    auto [reply] = run(client.on(loop).get(server.url("/hop/0")).send());
    ASSERT(reply.has_value());
    EXPECT(reply->status == 302);
    EXPECT(reply->text() == "redirect");
    EXPECT(reply->url == server.url("/hop/0"));
    EXPECT(reply->header_value("location") == server.url("/final"));
    EXPECT(server.requests().size() == 1U);
}

ZEST_CASE(redirects_up_to_the_limit_are_followed) {
    ASSERT(server.listening());
    auto client = test::loopback_client().redirect(redirect_policy::limited(1));

    auto [reply] = run(client.on(loop).get(server.url("/hop/0")).send());
    ASSERT(reply.has_value());
    EXPECT(reply->text() == "final");
}

ZEST_CASE(more_redirects_than_allowed_fails) {
    ASSERT(server.listening());
    auto client = test::loopback_client().redirect(redirect_policy::limited(1));

    auto [reply] = run(client.on(loop).get(server.url("/hop/1")).send());
    ASSERT(reply.has_error());
    EXPECT(reply.error().kind == error_kind::curl);
    EXPECT(reply.error().curl_code == CURLE_TOO_MANY_REDIRECTS);
}

ZEST_CASE(referer_goes_with_a_redirect) {
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/hop/0")).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 2U);
    EXPECT(server.requests()[0].count("referer") == 0U);
    EXPECT(server.requests()[1].header("referer") == server.url("/hop/0"));
}

ZEST_CASE(referer_false_leaves_it_out) {
    ASSERT(server.listening());
    auto client = test::loopback_client().referer(false);

    auto [reply] = run(client.on(loop).get(server.url("/hop/0")).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 2U);
    EXPECT(server.requests()[1].count("referer") == 0U);
}

};  // ZEST_SUITE(http_detail_request_settings_redirect)

}  // namespace

}  // namespace kota::http
