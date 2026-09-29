#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "http/harness/server.h"
#include "kota/http/detail/manager.h"
#include "kota/http/http.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

/// Answers each request with its target.
test::Reply echo_target(const test::Received& request) {
    return test::Reply{.body = request.target};
}

/// Waits for `signal`: a task, so that its cancellation can be caught.
task<> wait_for(event& signal) {
    co_await signal.wait();
}

ZEST_SUITE(http_detail_manager, test::LoopFixture) {

ZEST_CASE(requests_on_a_loop_share_its_manager) {
    test::HttpServer server(loop, echo_target);
    ASSERT(server.listening());
    http::client client;
    auto api = client.on(loop);

    auto [first] = run(api.get(server.url("/1")).send());
    auto* manager = &manager::for_loop(loop);
    auto [second] = run(http::client().on(loop).get(server.url("/2")).send());
    EXPECT(first.has_value());
    EXPECT(second.has_value());
    EXPECT((&manager::for_loop(loop) == manager));
    EXPECT((&manager->loop() == &loop));
    EXPECT((manager->native_multi() != nullptr));
    EXPECT(manager->pending_requests() == 0U);
}

ZEST_CASE(pending_requests_counts_the_requests_in_flight) {
    event arrived;
    event release;
    test::HttpServer server(loop, [&](const test::Received&) {
        arrived.set();
        return test::Reply{.hold = &release};
    });
    ASSERT(server.listening());
    http::client client;
    auto count = [&]() -> task<std::size_t> {
        co_await arrived.wait();
        auto pending = manager::for_loop(loop).pending_requests();
        release.set();
        co_return pending;
    };

    auto [reply, pending] = run(client.on(loop).get(server.url("/")).send(), count());
    EXPECT(reply.has_value());
    ASSERT(pending.has_value());
    EXPECT(*pending == 1U);
    EXPECT(manager::for_loop(loop).pending_requests() == 0U);
}

ZEST_CASE(many_requests_at_once_all_complete) {
    constexpr int count = 64;
    test::HttpServer server(loop, echo_target);
    ASSERT(server.listening());
    http::client client;
    auto api = client.on(loop);
    auto all = [&]() -> task<std::vector<std::string>, error> {
        std::vector<task<response, error>> sends;
        for(int i = 0; i < count; ++i) {
            sends.push_back(api.get(server.url(std::format("/{}", i))).send());
        }
        auto replies = co_await or_fail(co_await when_all(std::move(sends)));
        std::vector<std::string> texts;
        for(auto& reply: replies) {
            texts.push_back(reply.text_copy());
        }
        co_return texts;
    };

    auto [texts] = run(all());
    ASSERT(texts.has_value());
    ASSERT(texts->size() == std::size_t(count));
    for(int i = 0; i < count; ++i) {
        EXPECT((*texts)[i] == std::format("/{}", i));
    }
    EXPECT(server.requests().size() == std::size_t(count));
    EXPECT(manager::for_loop(loop).pending_requests() == 0U);
}

ZEST_CASE(request_sent_after_a_wait_on_another_task_completes) {
    test::HttpServer server(loop, echo_target);
    ASSERT(server.listening());
    http::client client;
    auto api = client.on(loop);
    event first_done;
    auto first = [&]() -> task<std::string, error> {
        auto reply = co_await api.get(server.url("/first")).send().or_fail();
        first_done.set();
        co_return reply.text_copy();
    };
    auto second = [&]() -> task<std::string, error> {
        co_await first_done.wait();
        auto reply = co_await api.get(server.url("/second")).send().or_fail();
        co_return reply.text_copy();
    };

    auto [one, two] = run(first(), second());
    ASSERT(one.has_value());
    ASSERT(two.has_value());
    EXPECT(*one == "/first");
    EXPECT(*two == "/second");
}

ZEST_CASE(cancelled_request_leaves_the_manager_working) {
    event arrived;
    event never;
    test::HttpServer server(loop, [&](const test::Received& request) {
        if(request.target == "/held") {
            arrived.set();
            return test::Reply{.hold = &never};
        }
        return echo_target(request);
    });
    ASSERT(server.listening());
    http::client client;
    auto api = client.on(loop);
    auto race = [&]() -> task<std::size_t, error> {
        auto won = co_await when_any(api.get(server.url("/held")).send(), wait_for(arrived));
        if(won.has_error()) {
            co_await fail(std::move(won).error());
        }
        co_return won->index();
    };

    auto [raced] = run(race());
    ASSERT(raced.has_value());
    EXPECT(*raced == 1U);
    EXPECT(manager::for_loop(loop).pending_requests() == 0U);

    auto [next] = run(api.get(server.url("/next")).send());
    ASSERT(next.has_value());
    EXPECT(next->text() == "/next");
}

ZEST_CASE(dropping_a_task_in_flight_cancels_its_request) {
    event arrived;
    event never;
    test::HttpServer server(loop, [&](const test::Received& request) {
        if(request.target == "/held") {
            arrived.set();
            return test::Reply{.hold = &never};
        }
        return echo_target(request);
    });
    ASSERT(server.listening());
    http::client client;
    auto api = client.on(loop);
    std::optional<task<response, error>> held(api.get(server.url("/held")).send());
    loop.schedule(*held);
    auto drop = [&]() -> task<std::size_t> {
        co_await arrived.wait();
        held.reset();
        co_return manager::for_loop(loop).pending_requests();
    };

    auto [pending] = run(drop());
    ASSERT(pending.has_value());
    EXPECT(*pending == 0U);

    auto [next] = run(api.get(server.url("/next")).send());
    ASSERT(next.has_value());
    EXPECT(next->text() == "/next");
}

ZEST_CASE(unregister_loop_aborts_the_requests_in_flight) {
    event arrived;
    event never;
    test::HttpServer server(loop, [&](const test::Received&) {
        arrived.set();
        return test::Reply{.hold = &never};
    });
    ASSERT(server.listening());
    http::client client;
    auto unregister = [&]() -> task<> {
        co_await arrived.wait();
        manager::unregister_loop(loop);
    };

    auto [reply, unregistered] = run(client.on(loop).get(server.url("/")).send(), unregister());
    ASSERT(reply.has_error());
    EXPECT(reply.error().kind == error_kind::aborted);
    EXPECT(reply.error().message() == "the event loop's http manager was destroyed");
}

// The next request makes a new manager, with nothing of the old one: its
// cookie jars go too.
ZEST_CASE(unregister_loop_starts_the_loop_afresh) {
    test::HttpServer server(loop, [](const test::Received& request) {
        if(request.target == "/seed") {
            return test::Reply{.headers = {{"Set-Cookie", "session=old; Path=/"}}};
        }
        return echo_target(request);
    });
    ASSERT(server.listening());
    http::client client;
    auto api = client.on(loop);
    auto flow = [&]() -> task<std::string, error> {
        co_await api.get(server.url("/seed")).send().or_fail();
        manager::unregister_loop(loop);
        auto reply = co_await api.get(server.url("/next")).send().or_fail();
        co_return reply.text_copy();
    };

    auto [text] = run(flow());
    ASSERT(text.has_value());
    EXPECT(*text == "/next");
    ASSERT(server.requests().size() == 2U);
    EXPECT(server.requests()[1].count("cookie") == 0U);
    EXPECT(manager::for_loop(loop).pending_requests() == 0U);
}

};  // ZEST_SUITE(http_detail_manager)

}  // namespace

}  // namespace kota::http
