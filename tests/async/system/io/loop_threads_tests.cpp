#include <cstddef>
#include <semaphore>
#include <thread>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_io_loop_threads, test::LoopFixture) {

ZEST_CASE(callback_sent_from_another_thread_runs_on_the_loop) {
    const auto loop_thread = std::this_thread::get_id();
    std::thread sender;
    auto receive = [&]() -> task<bool> {
        event done;
        bool on_loop_thread = false;
        sender = std::thread([&, r = loop.create_relay()]() mutable {
            r.send([&] {
                on_loop_thread = std::this_thread::get_id() == loop_thread;
                done.set();
            });
        });
        co_await done.wait();
        co_return on_loop_thread;
    };

    auto [result] = run(receive());
    sender.join();
    ASSERT(result.has_value());
    EXPECT(*result);
}

ZEST_CASE(callbacks_from_one_thread_arrive_in_order) {
    constexpr int count = 100;
    std::thread sender;
    auto receive = [&]() -> task<std::vector<int>> {
        event done;
        std::vector<int> order;
        sender = std::thread([&, r = loop.create_relay()]() mutable {
            for(int i = 0; i < count; ++i) {
                r.send([&, i] {
                    order.push_back(i);
                    if(i == count - 1) {
                        done.set();
                    }
                });
            }
        });
        co_await done.wait();
        co_return order;
    };

    auto [result] = run(receive());
    sender.join();
    ASSERT(result.has_value());
    ASSERT(result->size() == static_cast<std::size_t>(count));
    for(int i = 0; i < count; ++i) {
        ZEST_CONTEXT("callback {}", i);
        EXPECT((*result)[static_cast<std::size_t>(i)] == i);
    }
}

ZEST_CASE(concurrent_senders_deliver_every_callback) {
    constexpr int threads = 4;
    constexpr int per_thread = 25;
    std::vector<std::thread> senders;
    auto receive = [&]() -> task<int> {
        event done;
        int received = 0;
        auto r = loop.create_relay();
        for(int t = 0; t < threads; ++t) {
            senders.emplace_back([&] {
                for(int i = 0; i < per_thread; ++i) {
                    r.send([&] {
                        received += 1;
                        if(received == threads * per_thread) {
                            done.set();
                        }
                    });
                }
            });
        }
        // The relay must outlive every send(); the callbacks run once the
        // loop gets back to polling.
        for(auto& sender: senders) {
            sender.join();
        }
        co_await done.wait();
        co_return received;
    };

    auto [result] = run(receive());
    ASSERT(result.has_value());
    EXPECT(*result == threads * per_thread);
}

// The relay held here would keep the loop running for good; only stop()
// from the callback ends run(), with work still pending.
ZEST_CASE(callback_from_another_thread_can_stop_the_loop) {
    auto hold = loop.create_relay();
    bool stopped = false;
    std::thread sender([&, r = loop.create_relay()]() mutable {
        r.send([&] {
            stopped = true;
            loop.stop();
        });
    });

    EXPECT(loop.run() != 0);
    sender.join();
    EXPECT(stopped);
}

// A producer that sends and drops its relay while the loop is running an
// earlier callback must not lose the new one: the loop stays up for one
// more round.
ZEST_CASE(callback_sent_while_the_relay_goes_is_delivered) {
    std::binary_semaphore first_running{0};
    std::binary_semaphore second_sent{0};
    bool second_ran = false;
    std::thread sender([&, r = loop.create_relay()]() mutable {
        r.send([&] {
            first_running.release();
            second_sent.acquire();
        });
        first_running.acquire();
        r.send([&] { second_ran = true; });
        r = relay{};
        second_sent.release();
    });

    EXPECT(loop.run() == 0);
    sender.join();
    EXPECT(second_ran);
}

};  // ZEST_SUITE(async_io_loop_threads)

}  // namespace

}  // namespace kota
