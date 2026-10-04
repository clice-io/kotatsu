#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "http/harness/server.h"
#include "kota/http/detail/manager.h"
#include "kota/http/http.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

/// Answers each request with its target, in the body and in a header.
test::Reply echo_target(const test::Received& request) {
    return test::Reply{
        .headers = {{"X-Target", request.target}},
        .body = request.target,
    };
}

/// Waits for `signal`: a task, so that its cancellation can be caught.
task<> wait_for(event& signal) {
    co_await signal.wait();
}

ZEST_SUITE(http_detail_manager, zest::LoopFixture) {

ZEST_CASE(requests_on_a_loop_share_its_manager) {
    test::HttpServer server(loop, echo_target);
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);

    auto [first] = run(api.get(server.url("/1")).send());
    auto* first_manager = &manager::for_loop(loop);
    auto [second] = run(test::loopback_client().on(loop).get(server.url("/2")).send());
    ZEXPECT(first.has_value());
    ZEXPECT(second.has_value());
    ZEXPECT((&manager::for_loop(loop) == first_manager));
    ZEXPECT((&first_manager->loop() == &loop));
    ZEXPECT((first_manager->native_multi() != nullptr));
    ZEXPECT(first_manager->pending_requests() == 0U);
}

ZEST_CASE(pending_requests_counts_the_requests_in_flight) {
    event arrived;
    event release;
    test::HttpServer server(loop, [&](const test::Received&) {
        arrived.set();
        return test::Reply{.hold = &release};
    });
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto count = [&]() -> task<std::size_t> {
        co_await arrived.wait();
        auto pending = manager::for_loop(loop).pending_requests();
        release.set();
        co_return pending;
    };

    auto [reply, pending] = run(client.on(loop).get(server.url("/")).send(), count());
    ZEXPECT(reply.has_value());
    ZASSERT(pending.has_value());
    ZEXPECT(*pending == 1U);
    ZEXPECT(manager::for_loop(loop).pending_requests() == 0U);
}

ZEST_CASE(many_requests_at_once_all_complete) {
    constexpr int count = 64;
    test::HttpServer server(loop, echo_target);
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);
    auto all = [&]() -> task<std::vector<response>, error> {
        std::vector<task<response, error>> sends;
        for(int i = 0; i < count; ++i) {
            sends.push_back(api.get(server.url(std::format("/{}", i))).send());
        }
        auto replies = co_await or_fail(co_await when_all(std::move(sends)));
        co_return std::vector<response>(std::make_move_iterator(replies.begin()),
                                        std::make_move_iterator(replies.end()));
    };

    auto [replies] = run(all());
    ZASSERT(replies.has_value());
    ZASSERT(replies->size() == std::size_t(count));
    for(int i = 0; i < count; ++i) {
        ZEST_CONTEXT("request {}", i);
        const auto& reply = (*replies)[i];
        ZEXPECT(reply.text() == std::format("/{}", i));
        // Each response has its own headers.
        ZEXPECT(reply.header_value("x-target") == std::format("/{}", i));
    }
    ZEXPECT(server.requests().size() == std::size_t(count));
    ZEXPECT(manager::for_loop(loop).pending_requests() == 0U);
}

ZEST_CASE(request_sent_after_a_wait_on_another_task_completes) {
    test::HttpServer server(loop, echo_target);
    ZASSERT(server.listening());
    auto client = test::loopback_client();
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
    ZASSERT(one.has_value());
    ZASSERT(two.has_value());
    ZEXPECT(*one == "/first");
    ZEXPECT(*two == "/second");
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
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);
    auto race = [&]() -> task<std::size_t, error> {
        auto won = co_await when_any(api.get(server.url("/held")).send(), wait_for(arrived));
        if(won.has_error()) {
            co_await fail(std::move(won).error());
        }
        co_return won->index();
    };

    auto [raced] = run(race());
    ZASSERT(raced.has_value());
    ZEXPECT(*raced == 1U);
    ZEXPECT(manager::for_loop(loop).pending_requests() == 0U);

    auto [next] = run(api.get(server.url("/next")).send());
    ZASSERT(next.has_value());
    ZEXPECT(next->text() == "/next");
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
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);
    std::optional<task<response, error>> held(api.get(server.url("/held")).send());
    loop.schedule(*held);
    auto drop = [&]() -> task<std::size_t> {
        co_await arrived.wait();
        held.reset();
        co_return manager::for_loop(loop).pending_requests();
    };

    auto [pending] = run(drop());
    ZASSERT(pending.has_value());
    ZEXPECT(*pending == 0U);

    auto [next] = run(api.get(server.url("/next")).send());
    ZASSERT(next.has_value());
    ZEXPECT(next->text() == "/next");
}

// A request whose task is cancelled after the manager aborted it ends
// cancelled once the abort's queued completion runs.
ZEST_CASE(request_cancelled_after_unregister_loop_ends_cancelled) {
    event arrived;
    event never;
    test::HttpServer server(loop, [&](const test::Received& request) {
        if(request.target == "/held") {
            arrived.set();
            return test::Reply{.hold = &never};
        }
        return echo_target(request);
    });
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);
    auto sent = api.get(server.url("/held")).send();
    auto unregister_and_cancel = [&]() -> task<> {
        co_await arrived.wait();
        manager::unregister_loop(loop);
        sent.cancel();
    };

    auto [reply, cancelled] = run(sent, unregister_and_cancel());
    ZEXPECT(reply.is_cancelled());
    ZEXPECT(manager::for_loop(loop).pending_requests() == 0U);

    auto [next] = run(api.get(server.url("/next")).send());
    ZASSERT(next.has_value());
    ZEXPECT(next->text() == "/next");
}

// Both replies come on one turn, and the first one's task drops the
// other's, whose completion curl has queued already or is about to.
ZEST_CASE(dropping_a_task_whose_reply_came_leaves_the_manager_working) {
    event both_arrived;
    event release;
    int arrived = 0;
    test::HttpServer server(loop, [&](const test::Received& request) {
        auto reply = echo_target(request);
        if(request.target != "/next") {
            if(++arrived == 2) {
                both_arrived.set();
            }
            reply.hold = &release;
        }
        return reply;
    });
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);
    std::optional<task<response, error>> second(api.get(server.url("/second")).send());
    loop.schedule(*second);
    auto first = [&]() -> task<std::string, error> {
        auto reply = co_await api.get(server.url("/first")).send().or_fail();
        second.reset();
        co_return reply.text_copy();
    };
    auto release_both = [&]() -> task<> {
        co_await both_arrived.wait();
        release.set();
    };

    auto [text, released] = run(first(), release_both());
    ZASSERT(text.has_value());
    ZEXPECT(*text == "/first");
    ZEXPECT(!second.has_value());
    ZEXPECT(manager::for_loop(loop).pending_requests() == 0U);

    auto [next] = run(api.get(server.url("/next")).send());
    ZASSERT(next.has_value());
    ZEXPECT(next->text() == "/next");
}

ZEST_CASE(request_in_flight_at_unregister_loop_fails) {
    event arrived;
    event never;
    test::HttpServer server(loop, [&](const test::Received&) {
        arrived.set();
        return test::Reply{.hold = &never};
    });
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto unregister = [&]() -> task<> {
        co_await arrived.wait();
        manager::unregister_loop(loop);
    };

    auto [reply, unregistered] = run(client.on(loop).get(server.url("/")).send(), unregister());
    ZASSERT(reply.has_error());
    ZEXPECT(reply.error().kind == error_kind::aborted);
    ZEXPECT(reply.error().message() == "the event loop's http manager was destroyed");
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
    ZASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);
    auto flow = [&]() -> task<std::string, error> {
        co_await api.get(server.url("/seed")).send().or_fail();
        manager::unregister_loop(loop);
        auto reply = co_await api.get(server.url("/next")).send().or_fail();
        co_return reply.text_copy();
    };

    auto [text] = run(flow());
    ZASSERT(text.has_value());
    ZEXPECT(*text == "/next");
    ZASSERT(server.requests().size() == 2U);
    ZEXPECT(server.requests()[1].count("cookie") == 0U);
    ZEXPECT(manager::for_loop(loop).pending_requests() == 0U);
}

};  // ZEST_SUITE(http_detail_manager)

}  // namespace

}  // namespace kota::http
