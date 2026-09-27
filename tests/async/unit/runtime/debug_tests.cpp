#include <chrono>
#include <string>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_runtime_debug, test::LoopFixture) {

ZEST_CASE(dump_dot_draws_a_task_that_has_not_started) {
    auto make = []() -> task<int> {
        co_return 1;
    };
    auto pending = make();

    auto dot = dump_dot(pending);
    EXPECT(zest::starts_with(dot, "digraph async_graph {\n"));
    EXPECT(zest::contains(dot, "Task\nPending"));
    EXPECT(zest::ends_with(dot, "}\n"));

    pending->cancel();
    EXPECT(zest::contains(dump_dot(pending), "Task\nCancelled"));
}

// Drawn while the task is blocked: once it ends, run() frees its frame.
ZEST_CASE(dump_dot_follows_a_blocked_task_to_its_resource) {
    event gate;
    auto waiter = [&]() -> task<> {
        co_await gate.wait();
    };
    auto target = waiter();
    auto* node = target.operator->();
    auto inspect = [&]() -> task<std::string> {
        auto dot = dump_dot(*node);
        gate.set();
        co_return dot;
    };

    auto [waited, dot] = run(std::move(target), inspect());
    EXPECT(waited.has_value());
    ASSERT(dot.has_value());
    EXPECT(zest::contains(*dot, "Task\nRunning"));
    EXPECT(zest::contains(*dot, "EventWaiter"));
    EXPECT(zest::contains(*dot, R"(label="Event)"));
    EXPECT(zest::contains(*dot, "debug_tests.cpp:"));
    EXPECT(zest::contains(*dot, "->"));
}

// A root the test owns outlives its cancellation, so it can be drawn after:
// the cancelled wait is gone from its graph.
ZEST_CASE(dump_dot_drops_the_wait_of_a_cancelled_task) {
    event gate;
    auto waiter = [&]() -> task<> {
        co_await gate.wait();
    };
    auto root = waiter();
    std::string blocked;
    std::string cancelled;
    auto inspect = [&]() -> task<> {
        blocked = dump_dot(root);
        root->cancel();
        cancelled = dump_dot(root);
        co_return;
    };
    auto inspector = inspect();

    loop.schedule(root);
    loop.schedule(inspector);
    loop.run();
    EXPECT(zest::contains(blocked, "EventWaiter"));
    EXPECT(zest::contains(cancelled, "Task\nCancelled"));
    EXPECT(!zest::contains(cancelled, "EventWaiter"));
}

ZEST_CASE(dump_dot_labels_aggregates_and_io) {
    event gate;
    auto branch = [&]() -> task<> {
        co_await gate.wait();
    };
    auto sleeper = []() -> task<> {
        co_await sleep(std::chrono::hours(1));
    };
    auto race = [&]() -> task<> {
        co_await when_any(branch(), sleeper());
    };
    auto combined = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(branch());
        co_await when_all(race(), group.join());
    };
    auto target = combined();
    auto* node = target.operator->();
    auto inspect = [&]() -> task<std::string> {
        auto dot = dump_dot(*node);
        gate.set();
        co_return dot;
    };

    auto [combined_result, dot] = run(std::move(target), inspect());
    EXPECT(combined_result.has_value());
    ASSERT(dot.has_value());
    EXPECT(zest::contains(*dot, "WhenAll"));
    EXPECT(zest::contains(*dot, "WhenAny"));
    EXPECT(zest::contains(*dot, "TaskGroup"));
    EXPECT(zest::contains(*dot, "SystemIO"));
}

};  // ZEST_SUITE(async_runtime_debug)

}  // namespace

}  // namespace kota
