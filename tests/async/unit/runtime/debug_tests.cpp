#include <chrono>
#include <string>

#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_runtime_debug, zest::LoopFixture) {

// A cancel does not end a task that has not started: it still draws as
// pending.
ZEST_CASE(dump_dot_draws_a_task_that_has_not_started) {
    auto make = []() -> task<int> {
        co_return 1;
    };
    auto pending = make();

    auto dot = dump_dot(pending);
    ZEXPECT(zest::starts_with(dot, "digraph async_graph {\n"));
    ZEXPECT(zest::contains(dot, "Task\nPending"));
    ZEXPECT(zest::ends_with(dot, "}\n"));

    pending.cancel();
    ZEXPECT(zest::contains(dump_dot(pending), "Task\nPending"));
}

ZEST_CASE(dump_dot_follows_a_blocked_task_to_its_resource) {
    event gate;
    auto waiter = [&]() -> task<> {
        co_await gate.wait();
    };
    auto target = waiter();
    auto inspect = [&]() -> task<std::string> {
        auto dot = dump_dot(target);
        gate.set();
        co_return dot;
    };

    auto [waited, dot] = run(target, inspect());
    ZEXPECT(waited.has_value());
    ZASSERT(dot.has_value());
    ZEXPECT(zest::contains(*dot, "Task\nRunning"));
    ZEXPECT(zest::contains(*dot, "Waiter"));
    ZEXPECT(zest::contains(*dot, R"(label="Event)"));
    ZEXPECT(zest::contains(*dot, "debug_tests.cpp:"));
    ZEXPECT(zest::contains(*dot, "->"));
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
        root.cancel();
        cancelled = dump_dot(root);
        co_return;
    };
    auto inspector = inspect();

    loop.schedule(root);
    loop.schedule(inspector);
    loop.run();
    ZEXPECT(zest::contains(blocked, "Waiter"));
    ZEXPECT(zest::contains(cancelled, "Task\nCancelled"));
    ZEXPECT(!zest::contains(cancelled, "Waiter"));
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
        task_group<> group;
        group.spawn(branch());
        co_await when_all(race(), group.join());
    };
    auto target = combined();
    auto inspect = [&]() -> task<std::string> {
        auto dot = dump_dot(target);
        gate.set();
        co_return dot;
    };

    auto [combined_result, dot] = run(target, inspect());
    ZEXPECT(combined_result.has_value());
    ZASSERT(dot.has_value());
    ZEXPECT(zest::contains(*dot, "WhenAll"));
    ZEXPECT(zest::contains(*dot, "WhenAny"));
    ZEXPECT(zest::contains(*dot, "TaskGroup"));
    ZEXPECT(zest::contains(*dot, "SystemIO"));
}

};  // ZEST_SUITE(async_runtime_debug)

}  // namespace

}  // namespace kota
