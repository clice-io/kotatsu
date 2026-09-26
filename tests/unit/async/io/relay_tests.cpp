#include <atomic>
#include <thread>
#include <vector>

#include "../loop_fixture.h"
#include "kota/zest/zest.h"

namespace kota {

namespace {

ZEST_SUITE(event_loop_relay, loop_fixture){

    ZEST_CASE(relay_keeps_loop_alive){
        // A relay should keep the loop alive even with no other active handles.
        // Without the relay, the loop would exit immediately.
        bool called = false;

auto r = loop.create_relay();
std::thread worker([&, r = std::move(r)]() mutable { r.send([&] { called = true; }); });

loop.run();
worker.join();
EXPECT(called);

}  // namespace

ZEST_CASE(relay_cross_thread_send) {
    int value = 0;
    std::thread worker;

    auto t = [&]() -> task<> {
        event done;
        auto r = loop.create_relay();
        worker = std::thread([&, r = std::move(r)]() mutable {
            r.send([&] {
                value = 42;
                done.set();
            });
        });
        co_await done.wait();
    };

    auto task = t();
    schedule_all(task);
    worker.join();
    EXPECT(value == 42);
}

ZEST_CASE(relay_destroyed_without_send) {
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
    EXPECT(task_finished);
}

ZEST_CASE(relay_move_semantics) {
    bool called = false;

    auto r1 = loop.create_relay();
    auto r2 = std::move(r1);

    // r1 is moved-from, send on r2 should work.
    std::thread worker([&, r = std::move(r2)]() mutable { r.send([&] { called = true; }); });

    loop.run();
    worker.join();
    EXPECT(called);
}

ZEST_CASE(relay_multiple_send) {
    int counter = 0;

    auto t = [&]() -> task<> {
        event done;
        auto r = loop.create_relay();
        r.send([&] { counter++; });
        r.send([&] { counter++; });
        r.send([&] {
            counter++;
            done.set();
        });
        co_await done.wait();
    };

    auto task = t();
    schedule_all(task);
    EXPECT(counter == 3);
}

ZEST_CASE(relay_send_with_noop) {
    // Sending a no-op callback and then destroying the relay should
    // release the loop hold without crashing.
    auto r = loop.create_relay();

    std::thread worker([&, r = std::move(r)]() mutable { r.send([] {}); });

    loop.run();
    worker.join();
}

ZEST_CASE(relay_concurrent_send) {
    constexpr int N = 4;
    constexpr int M = 25;
    std::atomic<int> counter{0};

    auto t = [&]() -> task<> {
        event done;
        auto r = loop.create_relay();
        std::vector<std::thread> threads;
        for(int i = 0; i < N; ++i) {
            threads.emplace_back([&]() {
                for(int j = 0; j < M; ++j) {
                    r.send([&] {
                        if(counter.fetch_add(1, std::memory_order_relaxed) == N * M - 1) {
                            done.set();
                        }
                    });
                }
            });
        }
        for(auto& th: threads) {
            th.join();
        }
        co_await done.wait();
    };

    auto task = t();
    schedule_all(task);
    EXPECT(counter.load() == N * M);
}

ZEST_CASE(relay_stress_cross_thread) {
    int counter = 0;

    auto r = loop.create_relay();
    std::thread worker([&, r = std::move(r)]() mutable {
        for(int i = 0; i < 100; ++i) {
            r.send([&] { counter++; });
        }
    });

    loop.run();
    worker.join();
    EXPECT(counter == 100);
}

ZEST_CASE(relay_send_after_move) {
    bool called = false;

    auto t = [&]() -> task<> {
        auto r1 = loop.create_relay();
        auto r2 = std::move(r1);

        // r1 is moved-from (self == nullptr), send should be a safe no-op.
        r1.send([&] { called = true; });

        // Ensure the loop can still exit cleanly by destroying r2.
        co_return;
    };

    auto task = t();
    schedule_all(task);
    EXPECT(!called);
}

ZEST_CASE(relay_callback_stops_loop) {
    bool stopped = false;

    auto r = loop.create_relay();
    std::thread worker([&, r = std::move(r)]() mutable {
        r.send([&] {
            stopped = true;
            loop.stop();
        });
    });

    loop.run();
    worker.join();
    EXPECT(stopped);
}

ZEST_CASE(relay_fifo_order) {
    std::vector<int> order;

    auto t = [&]() -> task<> {
        event done;
        auto r = loop.create_relay();
        for(int i = 0; i < 5; ++i) {
            r.send([&, i] {
                order.push_back(i);
                if(i == 4) {
                    done.set();
                }
            });
        }
        co_await done.wait();
    };

    auto task = t();
    schedule_all(task);
    EXPECT(order == (std::vector<int>{0, 1, 2, 3, 4}));
}

ZEST_CASE(relay_pending_callbacks_delivered_after_destroy) {
    int counter = 0;

    auto t = [&]() -> task<> {
        event done;
        {
            auto r = loop.create_relay();
            r.send([&] { counter++; });
            r.send([&] {
                counter++;
                done.set();
            });
            // relay destroyed here; pending callbacks should still be delivered
        }
        co_await done.wait();
    };

    auto task = t();
    schedule_all(task);
    EXPECT(counter == 2);
}

ZEST_CASE(relay_send_during_drain) {
    int counter = 0;
    relay* shared = nullptr;

    auto t = [&]() -> task<> {
        event done;
        auto r = loop.create_relay();
        shared = &r;
        r.send([&] {
            counter++;
            shared->send([&] {
                counter++;
                done.set();
            });
        });
        co_await done.wait();
    };

    auto task = t();
    schedule_all(task);
    EXPECT(counter == 2);
}

ZEST_CASE(relay_move_assign_releases_old) {
    int old_counter = 0;
    int new_counter = 0;

    auto t = [&]() -> task<> {
        event done;
        auto r = loop.create_relay();
        r.send([&] { old_counter++; });

        // Move-assign overwrites r; old relay's pending callback should still run.
        r = loop.create_relay();
        r.send([&] {
            new_counter++;
            done.set();
        });
        co_await done.wait();
    };

    auto task = t();
    schedule_all(task);
    EXPECT(old_counter == 1);
    EXPECT(new_counter == 1);
}

ZEST_CASE(relay_multiple_keep_alive) {
    // Two relays: destroying one should not release the loop hold while the
    // other is still alive.
    bool called = false;

    auto r1 = loop.create_relay();
    auto r2 = loop.create_relay();

    // Destroy r1 immediately; r2 alone should still keep the loop alive.
    r1 = relay{};

    std::thread worker([&, r = std::move(r2)]() mutable { r.send([&] { called = true; }); });

    loop.run();
    worker.join();
    EXPECT(called);
}

// Regression: a producer that send()s and destroys its relay while the loop
// thread is mid-drain must not lose the callback. The first callback blocks
// the drain until the worker has enqueued the second callback and dropped the
// relay; the loop-hold release must observe the refilled queue and keep the
// loop alive for one more drain.
ZEST_CASE(relay_send_and_destroy_during_drain_delivers) {
    std::atomic<bool> first_running{false};
    std::atomic<bool> second_sent{false};
    bool second_ran = false;

    auto r = loop.create_relay();
    std::thread worker([&, r = std::move(r)]() mutable {
        r.send([&] {
            first_running.store(true, std::memory_order_release);
            while(!second_sent.load(std::memory_order_acquire)) {}
        });

        while(!first_running.load(std::memory_order_acquire)) {}
        r.send([&] { second_ran = true; });
        r = relay{};
        second_sent.store(true, std::memory_order_release);
    });

    loop.run();
    worker.join();
    EXPECT(second_ran);
}

};  // namespace kota

}  // namespace

}  // namespace kota
