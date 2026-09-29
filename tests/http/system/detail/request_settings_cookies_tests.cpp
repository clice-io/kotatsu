#include "async/harness/loop_fixture.h"
#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

/// Sets cookie `value` on /seed..., answers everything else plainly.
test::HttpServer::Handler seeding(std::string value) {
    return [value = std::move(value)](const test::Received& request) {
        if(request.target.starts_with("/seed")) {
            return test::Reply{.headers = {{"Set-Cookie", value + "; Path=/"}}};
        }
        return test::Reply{};
    };
}

ZEST_SUITE(http_detail_request_settings_cookies, test::LoopFixture) {

ZEST_CASE(cookie_a_reply_sets_goes_with_the_next_request) {
    test::HttpServer server(loop, seeding("session=alpha"));
    ASSERT(server.listening());
    http::client client;
    auto api = client.on(loop);

    auto [seed] = run(api.get(server.url("/seed")).send());
    auto [next] = run(api.get(server.url("/next")).send());
    EXPECT(seed.has_value());
    EXPECT(next.has_value());

    ASSERT(server.requests().size() == 2U);
    EXPECT(server.requests()[0].count("cookie") == 0U);
    EXPECT(server.requests()[1].header("cookie") == "session=alpha");
}

ZEST_CASE(cookies_are_sent_as_given) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = http::client().record_cookie(false);

    auto [reply] = run(client.on(loop).get(server.url("/")).cookies("a=1; b=two").send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("cookie") == "a=1; b=two");
}

ZEST_CASE(cookies_given_join_the_recorded_ones) {
    test::HttpServer server(loop, seeding("session=alpha"));
    ASSERT(server.listening());
    http::client client;
    auto api = client.on(loop);

    auto [seed] = run(api.get(server.url("/seed")).send());
    auto [next] = run(api.get(server.url("/next")).cookies("manual=1").send());
    EXPECT(seed.has_value());
    EXPECT(next.has_value());

    ASSERT(server.requests().size() == 2U);
    EXPECT(server.requests()[1].header("cookie") == "session=alpha; manual=1");
}

ZEST_CASE(record_cookie_false_neither_keeps_nor_sends_cookies) {
    test::HttpServer server(loop, [](const test::Received& request) {
        auto value = request.target == "/seed" ? "session=jar" : "session=other";
        return test::Reply{.headers = {{"Set-Cookie", std::string(value) + "; Path=/"}}};
    });
    ASSERT(server.listening());
    http::client client;

    auto [seed] = run(client.on(loop).get(server.url("/seed")).send());
    client.record_cookie(false);
    auto [off] = run(client.on(loop).get(server.url("/off")).send());
    client.record_cookie(true);
    auto [on] = run(client.on(loop).get(server.url("/on")).send());
    EXPECT(seed.has_value());
    EXPECT(off.has_value());
    EXPECT(on.has_value());

    ASSERT(server.requests().size() == 3U);
    EXPECT(server.requests()[1].count("cookie") == 0U);
    // The jar kept no cookie while it was off.
    EXPECT(server.requests()[2].header("cookie") == "session=jar");
}

ZEST_CASE(record_cookie_false_keeps_cookies_given) {
    test::HttpServer server(loop, seeding("session=jar"));
    ASSERT(server.listening());
    http::client client;

    auto [seed] = run(client.on(loop).get(server.url("/seed")).send());
    client.record_cookie(false);
    auto [next] = run(client.on(loop).get(server.url("/next")).cookies("manual=1").send());
    EXPECT(seed.has_value());
    EXPECT(next.has_value());

    ASSERT(server.requests().size() == 2U);
    EXPECT(server.requests()[1].header("cookie") == "manual=1");
}

ZEST_CASE(record_cookie_can_be_turned_off_for_one_request) {
    test::HttpServer server(loop, seeding("session=jar"));
    ASSERT(server.listening());
    http::client client;
    auto api = client.on(loop);

    auto [seed] = run(api.get(server.url("/seed")).send());
    auto [without] = run(api.get(server.url("/without")).record_cookie(false).send());
    auto [with] = run(api.get(server.url("/with")).send());
    EXPECT(seed.has_value());
    EXPECT(without.has_value());
    EXPECT(with.has_value());

    ASSERT(server.requests().size() == 3U);
    EXPECT(server.requests()[1].count("cookie") == 0U);
    EXPECT(server.requests()[2].header("cookie") == "session=jar");
}

};  // ZEST_SUITE(http_detail_request_settings_cookies)

}  // namespace

}  // namespace kota::http
