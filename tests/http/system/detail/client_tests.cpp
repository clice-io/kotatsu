#include <optional>
#include <string>
#include <utility>

#include "async/harness/loop_fixture.h"
#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_client, test::LoopFixture) {

ZEST_CASE(copies_share_a_jar) {
    test::HttpServer server(loop, test::seeding("session=shared"));
    ASSERT(server.listening());
    auto client = test::loopback_client();
    auto [seed] = run(client.on(loop).get(server.url("/seed")).send());
    auto copy = client;

    auto [next] = run(copy.on(loop).get(server.url("/next")).send());
    EXPECT(seed.has_value());
    EXPECT(next.has_value());

    ASSERT(server.requests().size() == 2U);
    EXPECT(server.requests()[1].header("cookie") == "session=shared");
}

ZEST_CASE(moved_client_keeps_the_jar) {
    test::HttpServer server(loop, test::seeding("session=moved"));
    ASSERT(server.listening());
    auto client = test::loopback_client();
    auto [seed] = run(client.on(loop).get(server.url("/seed")).send());
    http::client moved;
    moved = std::move(client);

    auto [next] = run(moved.on(loop).get(server.url("/next")).send());
    EXPECT(seed.has_value());
    EXPECT(next.has_value());

    ASSERT(server.requests().size() == 2U);
    EXPECT(server.requests()[1].header("cookie") == "session=moved");
}

ZEST_CASE(clients_keep_separate_jars) {
    test::HttpServer server(loop, test::seeding("session=left"));
    ASSERT(server.listening());
    auto left = test::loopback_client();
    auto right = test::loopback_client();

    auto [seed] = run(left.on(loop).get(server.url("/seed")).send());
    auto [from_right] = run(right.on(loop).get(server.url("/right")).send());
    auto [from_left] = run(left.on(loop).get(server.url("/left")).send());
    EXPECT(seed.has_value());
    EXPECT(from_right.has_value());
    EXPECT(from_left.has_value());

    ASSERT(server.requests().size() == 3U);
    EXPECT(server.requests()[1].count("cookie") == 0U);
    EXPECT(server.requests()[2].header("cookie") == "session=left");
}

// The loop drops the jar of a client that has gone as the next one comes.
ZEST_CASE(client_made_after_one_has_gone_starts_with_an_empty_jar) {
    test::HttpServer server(loop, test::seeding("session=gone"));
    ASSERT(server.listening());
    {
        auto gone = test::loopback_client();
        auto [seed] = run(gone.on(loop).get(server.url("/seed")).send());
        EXPECT(seed.has_value());
    }
    auto client = test::loopback_client();

    auto [next] = run(client.on(loop).get(server.url("/next")).send());
    EXPECT(next.has_value());

    ASSERT(server.requests().size() == 2U);
    EXPECT(server.requests()[1].count("cookie") == 0U);
}

ZEST_CASE(request_keeps_the_jar_of_a_client_gone) {
    test::HttpServer server(loop, test::seeding("session=kept"));
    ASSERT(server.listening());
    auto built = [&] {
        auto client = test::loopback_client();
        auto [seed] = run(client.on(loop).get(server.url("/seed")).send());
        EXPECT(seed.has_value());
        return client.on(loop).get(server.url("/next"));
    }();

    auto [next] = run(std::move(built).send());
    EXPECT(next.has_value());

    ASSERT(server.requests().size() == 2U);
    EXPECT(server.requests()[1].header("cookie") == "session=kept");
}

ZEST_CASE(bound_client_keeps_the_jar_of_a_client_gone) {
    test::HttpServer server(loop, test::seeding("session=kept"));
    ASSERT(server.listening());
    auto api = [&] {
        auto client = test::loopback_client();
        auto [seed] = run(client.on(loop).get(server.url("/seed")).send());
        EXPECT(seed.has_value());
        return client.on(loop);
    }();

    auto [next] = run(api.get(server.url("/next")).send());
    EXPECT(next.has_value());

    ASSERT(server.requests().size() == 2U);
    EXPECT(server.requests()[1].header("cookie") == "session=kept");
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
    ASSERT(server.listening());
    std::optional<http::client> client(test::loopback_client());
    auto [seed] = run(client->on(loop).get(server.url("/seed")).send());
    EXPECT(seed.has_value());

    auto sent = client->on(loop).get(server.url("/slow")).send();
    auto drop_client = [&]() -> task<> {
        co_await arrived.wait();
        client.reset();
        release.set();
    };
    auto [reply, dropped] = run(std::move(sent), drop_client());
    ASSERT(reply.has_value());
    EXPECT(reply->text() == "late");
    EXPECT(!client.has_value());

    ASSERT(server.requests().size() == 2U);
    EXPECT(server.requests()[1].header("cookie") == "session=flight");
}

ZEST_CASE(temporary_client_sends_through_on) {
    test::HttpServer server(loop);
    ASSERT(server.listening());

    auto [reply] =
        run(test::loopback_client().user_agent("temporary").on(loop).get(server.url("/")).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("user-agent") == "temporary");
}

ZEST_CASE(bound_client_keeps_the_settings_it_was_bound_with) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client().user_agent("bound");
    auto api = client.on(loop);
    client.user_agent("later");

    auto [reply] = run(api.get(server.url("/")).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("user-agent") == "bound");
}

ZEST_CASE(request_settings_override_the_clients) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    test::RefusingPort nowhere;
    ASSERT(nowhere.port > 0);
    auto client =
        test::loopback_client().proxy(nowhere.url()).user_agent("client").cookies("from=client");

    auto [reply] = run(client.on(loop)
                           .get(server.url("/"))
                           .no_proxy()
                           .user_agent("request")
                           .cookies("from=request")
                           .send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("user-agent") == "request");
    EXPECT(server.requests()[0].header("cookie") == "from=request");
}

};  // ZEST_SUITE(http_detail_client)

}  // namespace

}  // namespace kota::http
