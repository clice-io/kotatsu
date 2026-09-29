#include <format>
#include <latch>
#include <optional>
#include <string>
#include <utility>

#include "kota/http/detail/manager.h"
#include "kota/http/http.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

// Each case destroys a loop of its own with a request on it. The loop's
// manager goes first, and its requests in flight stay with their tasks,
// which end once cancelled; a request sent while the loop is being
// destroyed fails. ~event_loop runs the loop until the thread pool's work
// is done, and so resumes the task that queued it: the cases hold such work
// back until then, to act from inside ~event_loop. What those tasks use is
// declared before the loop, to outlive it.

namespace kota::http {

namespace {

/// A loopback listener that never accepts: the kernel takes connections for
/// it, and requests to it stay in flight.
struct Silent {
    tcp::acceptor listener;
    std::string url;
};

std::optional<Silent> silent(event_loop& loop) {
    auto listener = tcp::listen("127.0.0.1", 0, loop);
    if(!listener) {
        return std::nullopt;
    }
    auto name = listener->getsockname();
    if(!name) {
        return std::nullopt;
    }
    auto url = std::format("http://127.0.0.1:{}/", name->port);
    return Silent{.listener = std::move(*listener), .url = std::move(url)};
}

/// Stops `loop` once the tasks scheduled before it have started.
task<> stop(event_loop& loop) {
    loop.stop();
    co_return;
}

ZEST_SUITE(http_detail_manager_teardown) {

ZEST_CASE(request_that_outlives_its_loop_ends_when_cancelled) {
    http::client client;
    std::optional<event_loop> dying(std::in_place);
    auto target = silent(*dying);
    ASSERT(target.has_value());
    auto kept = client.on(*dying).get(target->url).send();
    dying->schedule(kept);
    dying->schedule(stop(*dying));
    dying->run();
    EXPECT(manager::for_loop(*dying).pending_requests() == 1U);

    dying.reset();
    ASSERT(!kept.done());
    kept.cancel();
    EXPECT(kept.done());
    EXPECT(kept.is_cancelled());
}

ZEST_CASE(request_cancelled_while_its_loop_is_destroyed_ends) {
    std::latch gate(1);
    http::client client;
    std::optional<event_loop> dying(std::in_place);
    auto target = silent(*dying);
    ASSERT(target.has_value());
    auto api = client.on(*dying);
    // Fails from inside ~event_loop, and when_all cancels the request.
    auto held_back = [&]() -> task<void, error> {
        co_await queue([&] { gate.wait(); }, *dying);
        co_await fail(error::aborted("held back"));
    };
    auto both = [&]() -> task<void, error> {
        co_await or_fail(co_await when_all(held_back(), api.get(target->url).send()));
    };
    auto kept = both();
    dying->schedule(kept);
    dying->schedule(stop(*dying));
    dying->run();

    gate.count_down();
    dying.reset();
    ASSERT(kept.done());
    auto ended = kept.result();
    ASSERT(ended.has_error());
    EXPECT(ended.error().message() == "held back");
}

ZEST_CASE(request_sent_while_its_loop_is_destroyed_fails) {
    std::latch gate(1);
    std::optional<outcome<response, error>> sent;
    http::client client;
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

};  // ZEST_SUITE(http_detail_manager_teardown)

}  // namespace

}  // namespace kota::http
