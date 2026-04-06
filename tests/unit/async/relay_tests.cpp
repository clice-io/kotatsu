#include <atomic>
#include <thread>

#include "loop_fixture.h"
#include "eventide/zest/zest.h"

namespace eventide {

namespace {

TEST_SUITE(event_loop_relay, loop_fixture) {

TEST_CASE(relay_keeps_loop_alive) {
    // A relay should keep the loop alive even with no other active handles.
    // Without the relay, the loop would exit immediately.
    std::atomic<bool> called{false};

    auto r = loop.create_relay();
    // Post from same thread after a short delay via another thread.
    std::thread worker([&, r = std::move(r)]() mutable { r.send([&] { called.store(true); }); });

    loop.run();
    worker.join();
    EXPECT_TRUE(called.load());
}

TEST_CASE(relay_cross_thread_send) {
    std::atomic<int> value{0};

    auto t = [&]() -> task<> {
        auto r = loop.create_relay();
        std::thread([&, r = std::move(r)]() mutable { r.send([&] { value.store(42); }); }).detach();
        co_await sleep(100, loop);
        loop.stop();
    };

    auto task = t();
    schedule_all(task);
    EXPECT_EQ(value.load(), 42);
}

TEST_CASE(relay_destroyed_without_send) {
    // Destroying a relay without calling send() should release the loop hold
    // and allow the loop to exit normally.
    bool task_finished = false;

    auto t = [&]() -> task<> {
        {
            auto r = loop.create_relay();
            // r goes out of scope without send()
        }
        task_finished = true;
        co_return;
    };

    auto task = t();
    schedule_all(task);
    EXPECT_TRUE(task_finished);
}

TEST_CASE(relay_send_called_twice) {
    // Only the first send() should take effect.
    std::atomic<int> counter{0};

    auto r = loop.create_relay();
    // Capture a copy-safe wrapper: move relay into a shared_ptr so both
    // threads can attempt send().
    auto shared = std::make_shared<relay>(std::move(r));

    std::thread t1([&, shared]() { shared->send([&] { counter.fetch_add(1); }); });
    std::thread t2([&, shared]() { shared->send([&] { counter.fetch_add(1); }); });

    loop.run();
    t1.join();
    t2.join();
    EXPECT_EQ(counter.load(), 1);
}

TEST_CASE(relay_move_semantics) {
    std::atomic<bool> called{false};

    auto r1 = loop.create_relay();
    auto r2 = std::move(r1);

    // r1 is moved-from, send on r2 should work.
    std::thread worker([&, r = std::move(r2)]() mutable { r.send([&] { called.store(true); }); });

    loop.run();
    worker.join();
    EXPECT_TRUE(called.load());
}

TEST_CASE(relay_send_with_noop) {
    // Sending a no-op callback should just release the loop hold
    // without crashing.
    auto r = loop.create_relay();

    std::thread worker([&, r = std::move(r)]() mutable { r.send([] {}); });

    loop.run();
    worker.join();
}

};  // TEST_SUITE(event_loop_relay)

}  // namespace

}  // namespace eventide
