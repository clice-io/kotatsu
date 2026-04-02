#include <atomic>
#include <thread>

#include "loop_fixture.h"
#include "eventide/zest/zest.h"

namespace eventide {

namespace {

using namespace std::chrono;

TEST_SUITE(event_loop_post, loop_fixture) {

TEST_CASE(post_from_same_thread) {
    bool called = false;

    auto t = [&]() -> task<> {
        loop.post([&] { called = true; });
        // Yield so the async callback can fire.
        co_await sleep(milliseconds{1}, loop);
        loop.stop();
    };

    auto task = t();
    schedule_all(task);
    EXPECT_TRUE(called);
}

TEST_CASE(post_from_another_thread) {
    std::atomic<bool> called{false};

    auto t = [&]() -> task<> {
        std::thread worker([&] { loop.post([&] { called.store(true); }); });
        worker.detach();
        // Give the worker time to post.
        co_await sleep(milliseconds{50}, loop);
        loop.stop();
    };

    auto task = t();
    schedule_all(task);
    EXPECT_TRUE(called.load());
}

TEST_CASE(multiple_posts_from_another_thread) {
    std::atomic<int> counter{0};
    constexpr int N = 100;

    auto t = [&]() -> task<> {
        std::thread worker([&] {
            for(int i = 0; i < N; ++i) {
                loop.post([&] { counter.fetch_add(1); });
            }
        });
        worker.detach();
        co_await sleep(milliseconds{100}, loop);
        loop.stop();
    };

    auto task = t();
    schedule_all(task);
    EXPECT_EQ(counter.load(), N);
}

TEST_CASE(post_stops_loop) {
    auto t = [&]() -> task<> {
        std::thread worker([&] { loop.post([&] { loop.stop(); }); });
        worker.detach();
        // Loop will be stopped by the posted callback.
        co_await sleep(milliseconds{500}, loop);
    };

    auto task = t();
    schedule_all(task);
}

};  // TEST_SUITE(event_loop_post)

}  // namespace

}  // namespace eventide
