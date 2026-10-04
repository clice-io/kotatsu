#include <string>

#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_request_settings_cookies, zest::LoopFixture) {

ZEST_CASE(cookie_a_reply_sets_goes_with_the_next_request) {
    test::HttpServer server(loop, test::seeding("session=alpha"));
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);

    auto [seed] = run(api.get(server.url("/seed")).send());
    auto [next] = run(api.get(server.url("/next")).send());
    ZEXPECT(seed.has_value());
    ZEXPECT(next.has_value());

    ZASSERT(server.requests().size() == 2U);
    ZEXPECT(server.requests()[0].count("cookie") == 0U);
    ZEXPECT(server.requests()[1].header("cookie") == "session=alpha");
}

ZEST_CASE(cookies_are_sent_as_given) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());
    auto client = test::loopback_client().record_cookie(false);

    auto [reply] = run(client.on(loop).get(server.url("/")).cookies("a=1; b=two").send());
    ZEXPECT(reply.has_value());

    ZASSERT(server.requests().size() == 1U);
    ZEXPECT(server.requests()[0].header("cookie") == "a=1; b=two");
}

ZEST_CASE(cookies_given_join_the_recorded_ones) {
    test::HttpServer server(loop, test::seeding("session=alpha"));
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);

    auto [seed] = run(api.get(server.url("/seed")).send());
    auto [next] = run(api.get(server.url("/next")).cookies("manual=1").send());
    ZEXPECT(seed.has_value());
    ZEXPECT(next.has_value());

    ZASSERT(server.requests().size() == 2U);
    ZEXPECT(server.requests()[1].header("cookie") == "session=alpha; manual=1");
}

ZEST_CASE(record_cookie_false_neither_keeps_nor_sends_cookies) {
    test::HttpServer server(loop, [](const test::Received& request) {
        auto value = request.target == "/seed" ? "session=jar" : "session=other";
        return test::Reply{.headers = {{"Set-Cookie", std::string(value) + "; Path=/"}}};
    });
    ZASSERT(server.listening());
    auto client = test::loopback_client();

    auto [seed] = run(client.on(loop).get(server.url("/seed")).send());
    client.record_cookie(false);
    auto [off] = run(client.on(loop).get(server.url("/off")).send());
    client.record_cookie(true);
    auto [on] = run(client.on(loop).get(server.url("/on")).send());
    ZEXPECT(seed.has_value());
    ZEXPECT(off.has_value());
    ZEXPECT(on.has_value());

    ZASSERT(server.requests().size() == 3U);
    ZEXPECT(server.requests()[1].count("cookie") == 0U);
    // The jar kept no cookie while it was off.
    ZEXPECT(server.requests()[2].header("cookie") == "session=jar");
}

ZEST_CASE(record_cookie_false_keeps_cookies_given) {
    test::HttpServer server(loop, test::seeding("session=jar"));
    ZASSERT(server.listening());
    auto client = test::loopback_client();

    auto [seed] = run(client.on(loop).get(server.url("/seed")).send());
    client.record_cookie(false);
    auto [next] = run(client.on(loop).get(server.url("/next")).cookies("manual=1").send());
    ZEXPECT(seed.has_value());
    ZEXPECT(next.has_value());

    ZASSERT(server.requests().size() == 2U);
    ZEXPECT(server.requests()[1].header("cookie") == "manual=1");
}

ZEST_CASE(record_cookie_can_be_turned_off_for_one_request) {
    test::HttpServer server(loop, test::seeding("session=jar"));
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);

    auto [seed] = run(api.get(server.url("/seed")).send());
    auto [without] = run(api.get(server.url("/without")).record_cookie(false).send());
    auto [with] = run(api.get(server.url("/with")).send());
    ZEXPECT(seed.has_value());
    ZEXPECT(without.has_value());
    ZEXPECT(with.has_value());

    ZASSERT(server.requests().size() == 3U);
    ZEXPECT(server.requests()[1].count("cookie") == 0U);
    ZEXPECT(server.requests()[2].header("cookie") == "session=jar");
}

};  // ZEST_SUITE(http_detail_request_settings_cookies)

}  // namespace

}  // namespace kota::http
