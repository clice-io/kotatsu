// ZEST_SUITE(async_runtime_task_group_basics): spawn/join and value passing for task_group
// — basic spawns, empty join, sleeping children, nesting with when_all in both
// directions, all-success with an error type, and stress with many tasks.
// Cancellation lives in cancel.cpp; errors/exceptions in errors.cpp; frame
// lifetime / settled-spawn rejection in lifetime.cpp.
#include "async/harness/loop_fixture.h"
#include "async/harness/support.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

ZEST_SUITE(task_group_basics, loop_fixture) {

ZEST_CASE(basic) {
    int count = 0;

    auto work = [&](int val) -> task<> {
        count += val;
        co_return;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(work(1));
        group.spawn(work(10));
        group.spawn(work(100));
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
    EXPECT(count == 111);
}

ZEST_CASE(empty_join) {
    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
}

ZEST_CASE(multiple_spawns) {
    int count = 0;

    auto inc1 = [&]() -> task<> {
        count += 1;
        co_return;
    };

    auto inc10 = [&]() -> task<> {
        count += 10;
        co_return;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(inc1());
        group.spawn(inc10());
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
    EXPECT(count == 11);
}

ZEST_CASE(with_sleep) {
    int count = 0;

    auto work = [&](int val, int ms) -> task<> {
        co_await sleep(ms, loop);
        count += val;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(work(1, 5));
        group.spawn(work(10, 1));
        group.spawn(work(100, 3));
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
    EXPECT(count == 111);
}

ZEST_CASE(in_when_all) {
    int group_count = 0;

    auto grouped_work = [&]() -> task<int> {
        task_group<> group(loop);
        auto work = [&]() -> task<> {
            co_await sleep(1, loop);
            group_count += 1;
        };
        for(int i = 0; i < 3; ++i) {
            group.spawn(work());
        }
        co_await group.join();
        co_return group_count;
    };

    auto normal = [&]() -> task<int> {
        co_await sleep(1, loop);
        co_return 100;
    };

    auto combined = [&]() -> task<int> {
        auto [a, b] = co_await when_all(grouped_work(), normal());
        co_return a + b;
    };

    auto t = combined();
    schedule_all(t);
    EXPECT(group_count == 3);
    EXPECT(t.result() == 103);
}

ZEST_CASE(when_all_in_group) {
    int count = 0;

    auto pair_work = [&]() -> task<> {
        auto a = [&]() -> task<int> {
            co_await sleep(1, loop);
            co_return 1;
        };
        auto b = [&]() -> task<int> {
            co_await sleep(1, loop);
            co_return 2;
        };
        auto [x, y] = co_await when_all(a(), b());
        count += x + y;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(pair_work());
        group.spawn(pair_work());
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
    EXPECT(count == 6);
}

ZEST_CASE(all_success_with_error_type) {
    auto ok = [&](int ms, int val) -> task<int, error> {
        co_await sleep(ms, loop);
        co_return val;
    };

    auto driver = [&]() -> task<> {
        task_group<error> group(loop);
        group.spawn(ok(1, 10));
        group.spawn(ok(1, 20));
        group.spawn(ok(1, 30));
        auto res = co_await group.join();
        EXPECT(res);
    };

    auto t = driver();
    schedule_all(t);
    EXPECT(t->is_finished());
}

// Stress test: 100+ tasks
ZEST_CASE(stress_many_tasks) {
    int count = 0;

    auto work = [&]() -> task<> {
        count += 1;
        co_return;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        for(int i = 0; i < 200; ++i) {
            group.spawn(work());
        }
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
    EXPECT(count == 200);
}

ZEST_CASE(stress_many_tasks_with_sleep) {
    int count = 0;

    auto work = [&]() -> task<> {
        co_await sleep(1, loop);
        count += 1;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        for(int i = 0; i < 100; ++i) {
            group.spawn(work());
        }
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
    EXPECT(count == 100);
}

};  // ZEST_SUITE(task_group_basics)

}  // namespace kota
