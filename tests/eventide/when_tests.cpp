#include "eventide/zest/zest.h"
#include "eventide/async/loop.h"
#include "eventide/async/task.h"
#include "eventide/async/watcher.h"
#include "eventide/async/when.h"

namespace eventide {

namespace {

TEST_SUITE(when_ops) {

TEST_CASE(when_all_values) {
    auto a = []() -> task<int> {
        co_return 1;
    };

    auto b = []() -> task<int> {
        co_return 2;
    };

    auto combined = [&]() -> task<int> {
        auto [x, y] = co_await when_all(a(), b());
        co_return x + y;
    };

    auto [res] = run(combined());
    EXPECT_EQ(res, 3);
}

TEST_CASE(any_first_wins) {
    int a_count = 0;
    int b_count = 0;

    auto a = [&]() -> task<int> {
        a_count += 1;
        co_return 10;
    };

    auto b = [&]() -> task<int> {
        b_count += 1;
        co_return 20;
    };

    auto combined = [&]() -> task<std::size_t> {
        auto idx = co_await when_any(a(), b());
        co_return idx;
    };

    auto [idx] = run(combined());
    EXPECT_EQ(idx, 0U);
    EXPECT_EQ(a_count, 1);
    EXPECT_EQ(b_count, 0);
}

TEST_CASE(all_sleep_values) {
    event_loop loop;
    int slow_done = 0;
    int fast_done = 0;

    auto slow = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{5}, loop);
        slow_done += 1;
        co_return 7;
    };

    auto fast = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        fast_done += 1;
        co_return 9;
    };

    auto combined = [&]() -> task<int> {
        auto [a, b] = co_await when_all(slow(), fast());
        co_return a + b;
    };

    auto task = combined();
    loop.schedule(task);
    loop.run();

    EXPECT_EQ(task.result(), 16);
    EXPECT_EQ(slow_done, 1);
    EXPECT_EQ(fast_done, 1);
}

TEST_CASE(any_sleep_wins) {
    event_loop loop;
    int fast_done = 0;
    int slow_done = 0;

    auto fast = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        fast_done += 1;
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{10}, loop);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<std::size_t> {
        auto idx = co_await when_any(fast(), slow());
        co_return idx;
    };

    auto task = combined();
    loop.schedule(task);
    loop.run();

    EXPECT_EQ(task.result(), 0U);
    EXPECT_EQ(fast_done, 1);
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(any_child_cancel) {
    event_loop loop;
    int cancel_started = 0;
    int slow_started = 0;
    int slow_done = 0;

    auto canceler = [&]() -> task<int> {
        cancel_started += 1;
        co_await sleep(std::chrono::milliseconds{1}, loop);
        co_await cancel();
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        slow_started += 1;
        co_await sleep(std::chrono::milliseconds{5}, loop);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<> {
        co_await when_any(slow(), canceler());
    };

    auto task = combined();
    loop.schedule(task);
    loop.run();

    EXPECT_TRUE(task->is_cancelled());
    EXPECT_EQ(cancel_started, 1);
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(all_cancel_others) {
    event_loop loop;
    int cancel_started = 0;
    int slow_started = 0;
    int slow_done = 0;

    auto canceler = [&]() -> task<int> {
        cancel_started += 1;
        co_await sleep(std::chrono::milliseconds{1}, loop);
        co_await cancel();
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        slow_started += 1;
        co_await sleep(std::chrono::milliseconds{5}, loop);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<> {
        co_await when_all(slow(), canceler());
    };

    auto task = combined();
    loop.schedule(task);
    loop.run();

    EXPECT_TRUE(task->is_cancelled());
    EXPECT_EQ(cancel_started, 1);
    EXPECT_EQ(slow_done, 0);
}

};  // TEST_SUITE(when_ops)

}  // namespace

}  // namespace eventide
