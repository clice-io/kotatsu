#include <tuple>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_runtime_task_group_join, test::LoopFixture) {

ZEST_CASE(join_waits_for_every_child) {
    event gates[3];
    int sum = 0;
    auto child = [&](int value, event& gate) -> task<> {
        co_await gate.wait();
        sum += value;
    };
    auto driver = [&]() -> task<int> {
        task_group<> group(loop);
        group.spawn(child(1, gates[0]));
        group.spawn(child(10, gates[1]));
        group.spawn(child(100, gates[2]));
        co_await group.join();
        co_return sum;
    };
    auto releaser = [&]() -> task<> {
        gates[2].set();
        co_await yield();
        gates[0].set();
        co_await yield();
        gates[1].set();
    };

    auto [result, drove] = run(driver(), releaser());
    ASSERT(result.has_value());
    EXPECT(*result == 111);
}

ZEST_CASE(spawn_runs_the_child_until_it_suspends) {
    std::vector<int> order;
    auto child = [&]() -> task<> {
        order.push_back(1);
        co_await yield();
        order.push_back(3);
    };
    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(child());
        order.push_back(2);
        co_await group.join();
    };

    auto [result] = run(driver());
    EXPECT(result.has_value());
    EXPECT(order == std::vector{1, 2, 3});
}

ZEST_CASE(children_that_finish_at_once_are_joined) {
    int sum = 0;
    auto child = [&](int value) -> task<> {
        sum += value;
        co_return;
    };
    auto driver = [&]() -> task<int> {
        task_group<> group(loop);
        group.spawn(child(1));
        group.spawn(child(10));
        co_await group.join();
        co_return sum;
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(*result == 11);
}

ZEST_CASE(join_of_an_empty_group_completes_at_once) {
    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        co_await group.join();
    };

    auto [result] = run(driver());
    EXPECT(result.has_value());
}

ZEST_CASE(join_of_an_error_group_without_failures_has_no_error) {
    auto child = [](int value) -> task<int, error> {
        co_await yield();
        co_return value;
    };
    auto driver = [&]() -> task<bool> {
        task_group<error> group(loop);
        group.spawn(child(10));
        group.spawn(child(20));
        auto joined = co_await group.join();
        co_return joined.has_value();
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(*result);
}

ZEST_CASE(group_joins_inside_when_all) {
    int grouped = 0;
    auto work = [&]() -> task<> {
        co_await yield();
        grouped += 1;
    };
    auto grouped_work = [&]() -> task<int> {
        task_group<> group(loop);
        for(int i = 0; i < 3; ++i) {
            group.spawn(work());
        }
        co_await group.join();
        co_return grouped;
    };
    auto normal = []() -> task<int> {
        co_await yield();
        co_return 100;
    };
    auto combined = [&]() -> task<std::tuple<int, int>> {
        co_return co_await when_all(grouped_work(), normal());
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(*result == std::tuple{3, 100});
}

ZEST_CASE(children_can_await_when_all) {
    int sum = 0;
    auto value = [](int v) -> task<int> {
        co_await yield();
        co_return v;
    };
    auto pair = [&]() -> task<> {
        auto [x, y] = co_await when_all(value(1), value(2));
        sum += x + y;
    };
    auto driver = [&]() -> task<int> {
        task_group<> group(loop);
        group.spawn(pair());
        group.spawn(pair());
        co_await group.join();
        co_return sum;
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(*result == 6);
}

ZEST_CASE(many_children_are_all_joined) {
    int finished = 0;
    auto at_once = [&]() -> task<> {
        finished += 1;
        co_return;
    };
    auto later = [&]() -> task<> {
        co_await yield();
        finished += 1;
    };
    auto driver = [&]() -> task<int> {
        task_group<> group(loop);
        for(int i = 0; i < 200; ++i) {
            group.spawn(at_once());
            group.spawn(later());
        }
        co_await group.join();
        co_return finished;
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(*result == 400);
}

};  // ZEST_SUITE(async_runtime_task_group_join)

}  // namespace

}  // namespace kota
