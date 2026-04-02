#include <atomic>
#include <thread>

#include "loop_fixture.h"
#include "eventide/zest/zest.h"

namespace eventide {

namespace {

TEST_SUITE(event_loop_post, loop_fixture) {

TEST_CASE(post_from_same_thread) {
    bool called = false;

    auto t = [&]() -> task<> {
        loop.post([&] { called = true; });
        // Yield so the async callback can fire.
        co_await sleep(1, loop);
        loop.stop();
    };

    auto task = t();
    schedule_all(task);
    EXPECT_TRUE(called);
}

TEST_CASE(post_from_another_thread) {
    std::atomic<bool> called{false};
    std::thread worker;

    auto t = [&]() -> task<> {
        worker = std::thread([&] { loop.post([&] { called.store(true); }); });
        co_await sleep(50, loop);
        loop.stop();
    };

    auto task = t();
    schedule_all(task);
    worker.join();
    EXPECT_TRUE(called.load());
}

TEST_CASE(multiple_posts_from_another_thread) {
    std::atomic<int> counter{0};
    std::thread worker;
    constexpr int N = 100;

    auto t = [&]() -> task<> {
        worker = std::thread([&] {
            for(int i = 0; i < N; ++i) {
                loop.post([&] { counter.fetch_add(1); });
            }
        });
        co_await sleep(100, loop);
        loop.stop();
    };

    auto task = t();
    schedule_all(task);
    worker.join();
    EXPECT_EQ(counter.load(), N);
}

TEST_CASE(post_stops_loop) {
    std::thread worker;

    auto t = [&]() -> task<> {
        worker = std::thread([&] { loop.post([&] { loop.stop(); }); });
        // Loop will be stopped by the posted callback.
        co_await sleep(500, loop);
    };

    auto task = t();
    schedule_all(task);
    worker.join();
}

};  // TEST_SUITE(event_loop_post)

}  // namespace

}  // namespace eventide
