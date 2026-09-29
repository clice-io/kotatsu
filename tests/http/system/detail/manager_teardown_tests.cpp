#include <latch>
#include <optional>
#include <string>
#include <utility>

#include "http/harness/server.h"
#include "kota/http/detail/manager.h"
#include "kota/http/http.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

// Each case destroys a loop of its own. The loop's manager goes first, and
// its requests in flight stay with their tasks, which end once cancelled; a
// request sent while the loop is being destroyed fails. ~event_loop runs the
// loop until the thread pool's work is done, and so resumes the task that
// queued it: two cases hold such work back to act from inside ~event_loop.
// What those tasks use is declared before the loop, to outlive it.

namespace kota::http {

namespace {

/// Stops `loop` once the tasks scheduled before it have started.
task<> stop(event_loop& loop) {
    loop.stop();
    co_return;
}

/// Stops `loop` once `signal` is set.
task<> stop_when(event& signal, event_loop& loop) {
    co_await signal.wait();
    loop.stop();
}

/// A request left in flight on a loop: `send` sends it to a server that
/// holds its reply for good, and once it has arrived the server goes while
/// the loop lives, before curl sees the connection close.
struct InFlight {
    event arrived;
    event never;

    template <typename Send>
    void leave(event_loop& loop, Send send) {
        test::HttpServer server(loop, [&](const test::Received&) {
            arrived.set();
            return test::Reply{.hold = &never};
        });
        if(!server.listening()) {
            return;
        }
        send(server.url("/"));
        auto stopping = stop_when(arrived, loop);
        loop.schedule(stopping);
        loop.run();
    }
};

ZEST_SUITE(http_detail_manager_teardown) {

ZEST_CASE(request_that_outlives_its_loop_ends_when_cancelled) {
    auto client = test::loopback_client();
    InFlight flight;
    std::optional<task<response, error>> kept;
    std::optional<event_loop> dying(std::in_place);
    flight.leave(*dying, [&](std::string url) {
        kept.emplace(client.on(*dying).get(std::move(url)).send());
        dying->schedule(*kept);
    });
    ASSERT(kept.has_value());
    EXPECT(manager::for_loop(*dying).pending_requests() == 1U);

    dying.reset();
    ASSERT(!kept->done());
    kept->cancel();
    EXPECT(kept->done());
    EXPECT(kept->is_cancelled());
}

ZEST_CASE(request_cancelled_while_its_loop_is_destroyed_ends) {
    std::latch gate(1);
    auto client = test::loopback_client();
    InFlight flight;
    std::optional<task<void, error>> kept;
    std::optional<event_loop> dying(std::in_place);
    auto api = client.on(*dying);
    // Fails from inside ~event_loop, and when_all cancels the request.
    auto held_back = [&]() -> task<void, error> {
        co_await queue([&] { gate.wait(); }, *dying);
        co_await fail(error::aborted("held back"));
    };
    auto both = [&](std::string url) -> task<void, error> {
        co_await or_fail(co_await when_all(held_back(), api.get(std::move(url)).send()));
    };
    flight.leave(*dying, [&](std::string url) {
        kept.emplace(both(std::move(url)));
        dying->schedule(*kept);
    });
    ASSERT(kept.has_value());
    EXPECT(manager::for_loop(*dying).pending_requests() == 1U);

    gate.count_down();
    dying.reset();
    ASSERT(kept->done());
    auto ended = kept->result();
    ASSERT(ended.has_error());
    EXPECT(ended.error().message() == "held back");
}

ZEST_CASE(request_sent_while_its_loop_is_destroyed_fails) {
    std::latch gate(1);
    std::optional<outcome<response, error>> sent;
    auto client = test::loopback_client();
    std::optional<event_loop> dying(std::in_place);
    auto api = client.on(*dying);
    auto late = [&]() -> task<> {
        co_await queue([&] { gate.wait(); }, *dying);
        sent.emplace(co_await api.get("http://127.0.0.1:1/").send());
    };
    dying->schedule(late());
    dying->schedule(stop(*dying));
    dying->run();

    gate.count_down();
    dying.reset();
    ASSERT(sent.has_value());
    ASSERT(sent->has_error());
    EXPECT(sent->error().kind == error_kind::aborted);
    EXPECT(sent->error().message() == "the event loop is being destroyed");
}

// emplace() makes the new loop in the old one's storage, at its address:
// the new loop must get a manager of its own.
ZEST_CASE(loop_made_where_one_was_destroyed_gets_a_new_manager) {
    auto client = test::loopback_client();
    std::optional<event_loop> loop(std::in_place);
    for(int round = 0; round < 2; ++round) {
        ZEST_CONTEXT("round {}", round);
        bool answered = false;
        {
            test::HttpServer server(*loop);
            ASSERT(server.listening());
            auto send = [&]() -> task<> {
                auto reply = co_await client.on(*loop).get(server.url("/")).send();
                answered = reply.has_value();
                loop->stop();
            };
            auto sending = send();
            loop->schedule(sending);
            loop->run();
        }
        EXPECT(answered);
        loop.emplace();
    }
}

};  // ZEST_SUITE(http_detail_manager_teardown)

}  // namespace

}  // namespace kota::http
