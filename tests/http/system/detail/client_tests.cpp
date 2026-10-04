#include <optional>
#include <string>
#include <utility>

#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_client, zest::LoopFixture) {

ZEST_CASE(copies_share_a_jar) {
    test::HttpServer server(loop, test::seeding("session=shared"));
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto [seed] = run(client.on(loop).get(server.url("/seed")).send());
    auto copy = client;

    auto [next] = run(copy.on(loop).get(server.url("/next")).send());
    ZEXPECT(seed.has_value());
    ZEXPECT(next.has_value());

    ZASSERT(server.requests().size() == 2U);
    ZEXPECT(server.requests()[1].header("cookie") == "session=shared");
}

ZEST_CASE(moved_client_keeps_the_jar) {
    test::HttpServer server(loop, test::seeding("session=moved"));
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto [seed] = run(client.on(loop).get(server.url("/seed")).send());
    http::client moved;
    moved = std::move(client);

    auto [next] = run(moved.on(loop).get(server.url("/next")).send());
    ZEXPECT(seed.has_value());
    ZEXPECT(next.has_value());

    ZASSERT(server.requests().size() == 2U);
    ZEXPECT(server.requests()[1].header("cookie") == "session=moved");
}

ZEST_CASE(clients_keep_separate_jars) {
    test::HttpServer server(loop, test::seeding("session=left"));
    ZASSERT(server.listening());
    auto left = test::loopback_client();
    auto right = test::loopback_client();

    auto [seed] = run(left.on(loop).get(server.url("/seed")).send());
    auto [from_right] = run(right.on(loop).get(server.url("/right")).send());
    auto [from_left] = run(left.on(loop).get(server.url("/left")).send());
    ZEXPECT(seed.has_value());
    ZEXPECT(from_right.has_value());
    ZEXPECT(from_left.has_value());

    ZASSERT(server.requests().size() == 3U);
    ZEXPECT(server.requests()[1].count("cookie") == 0U);
    ZEXPECT(server.requests()[2].header("cookie") == "session=left");
}

// The loop drops the jar of a client that has gone as the next one comes.
ZEST_CASE(client_made_after_one_has_gone_starts_with_an_empty_jar) {
    test::HttpServer server(loop, test::seeding("session=gone"));
    ZASSERT(server.listening());
    {
        auto gone = test::loopback_client();
        auto [seed] = run(gone.on(loop).get(server.url("/seed")).send());
        ZEXPECT(seed.has_value());
    }
    auto client = test::loopback_client();

    auto [next] = run(client.on(loop).get(server.url("/next")).send());
    ZEXPECT(next.has_value());

    ZASSERT(server.requests().size() == 2U);
    ZEXPECT(server.requests()[1].count("cookie") == 0U);
}

ZEST_CASE(request_keeps_the_jar_of_a_client_gone) {
    test::HttpServer server(loop, test::seeding("session=kept"));
    ZASSERT(server.listening());
    auto built = [&] {
        auto client = test::loopback_client();
        auto [seed] = run(client.on(loop).get(server.url("/seed")).send());
        ZEXPECT(seed.has_value());
        return client.on(loop).get(server.url("/next"));
    }();

    auto [next] = run(std::move(built).send());
    ZEXPECT(next.has_value());

    ZASSERT(server.requests().size() == 2U);
    ZEXPECT(server.requests()[1].header("cookie") == "session=kept");
}

ZEST_CASE(bound_client_keeps_the_jar_of_a_client_gone) {
    test::HttpServer server(loop, test::seeding("session=kept"));
    ZASSERT(server.listening());
    auto api = [&] {
        auto client = test::loopback_client();
        auto [seed] = run(client.on(loop).get(server.url("/seed")).send());
        ZEXPECT(seed.has_value());
        return client.on(loop);
    }();

    auto [next] = run(api.get(server.url("/next")).send());
    ZEXPECT(next.has_value());

    ZASSERT(server.requests().size() == 2U);
    ZEXPECT(server.requests()[1].header("cookie") == "session=kept");
}

ZEST_CASE(request_in_flight_outlives_its_client) {
    event arrived;
    event release;
    test::HttpServer server(loop, [&](const test::Received& request) {
        if(request.target == "/seed") {
            return test::Reply{.headers = {{"Set-Cookie", "session=flight; Path=/"}}};
        }
        arrived.set();
        return test::Reply{.body = "late", .hold = &release};
    });
    ZASSERT(server.listening());
    std::optional<http::client> client(test::loopback_client());
    auto [seed] = run(client->on(loop).get(server.url("/seed")).send());
    ZEXPECT(seed.has_value());

    auto sent = client->on(loop).get(server.url("/slow")).send();
    auto drop_client = [&]() -> task<> {
        co_await arrived.wait();
        client.reset();
        release.set();
    };
    auto [reply, dropped] = run(std::move(sent), drop_client());
    ZASSERT(reply.has_value());
    ZEXPECT(reply->text() == "late");
    ZEXPECT(!client.has_value());

    ZASSERT(server.requests().size() == 2U);
    ZEXPECT(server.requests()[1].header("cookie") == "session=flight");
}

ZEST_CASE(temporary_client_sends_through_on) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());

    auto [reply] =
        run(test::loopback_client().user_agent("temporary").on(loop).get(server.url("/")).send());
    ZEXPECT(reply.has_value());

    ZASSERT(server.requests().size() == 1U);
    ZEXPECT(server.requests()[0].header("user-agent") == "temporary");
}

ZEST_CASE(bound_client_keeps_the_settings_it_was_bound_with) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());
    auto client = test::loopback_client().user_agent("bound");
    auto api = client.on(loop);
    client.user_agent("later");

    auto [reply] = run(api.get(server.url("/")).send());
    ZEXPECT(reply.has_value());

    ZASSERT(server.requests().size() == 1U);
    ZEXPECT(server.requests()[0].header("user-agent") == "bound");
}

ZEST_CASE(request_settings_override_the_clients) {
    test::HttpServer server(loop);
    ZASSERT(server.listening());
    auto nowhere = test::refusing_url();
    ZASSERT(!nowhere.empty());
    auto client =
        test::loopback_client().proxy(nowhere).user_agent("client").cookies("from=client");

    auto [reply] = run(client.on(loop)
                           .get(server.url("/"))
                           .no_proxy()
                           .user_agent("request")
                           .cookies("from=request")
                           .send());
    ZEXPECT(reply.has_value());

    ZASSERT(server.requests().size() == 1U);
    ZEXPECT(server.requests()[0].header("user-agent") == "request");
    ZEXPECT(server.requests()[0].header("cookie") == "from=request");
}

};  // ZEST_SUITE(http_detail_client)

}  // namespace

}  // namespace kota::http
