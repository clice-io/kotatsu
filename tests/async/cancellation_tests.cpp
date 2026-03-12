#include <atomic>
#include <chrono>
#include <concepts>
#include <cstdlib>
#include <optional>
#include <thread>
#include <vector>

#include "eventide/zest/zest.h"
#include "eventide/async/cancellation.h"
#include "eventide/async/fs.h"
#include "eventide/async/loop.h"
#include "eventide/async/request.h"
#include "eventide/async/sync.h"
#include "eventide/async/watcher.h"
#include "eventide/async/when.h"

namespace eventide {

namespace {

int uv_thread_pool_size_for_test() {
    int value = 4;
    if(const char* raw = std::getenv("UV_THREADPOOL_SIZE"); raw != nullptr) {
        int parsed = std::atoi(raw);
        if(parsed > 0) {
            value = parsed;
        }
    }

    return value;
}

TEST_SUITE(cancellation) {

TEST_CASE(pass_through_value) {
    cancellation_source source;

    auto worker = []() -> task<int> {
        co_return 42;
    };

    auto [result] = run(with_token(worker(), source.token()));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
}

TEST_CASE(pre_cancel_skip) {
    cancellation_source source;
    source.cancel();

    int started = 0;
    auto worker = [&]() -> task<int> {
        started += 1;
        co_return 1;
    };

    auto [result] = run(with_token(worker(), source.token()));
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(started, 0);
}

TEST_CASE(cancel_in_flight) {
    event_loop loop;
    cancellation_source source;
    event gate;
    int started = 0;
    int finished = 0;

    auto worker = [&]() -> task<int> {
        started += 1;
        co_await gate.wait();
        finished += 1;
        co_return 7;
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        source.cancel();
    };

    auto releaser = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{2}, loop);
        gate.set();
    };

    auto guarded_task = with_token(worker(), source.token());
    auto cancel_task = canceler();
    auto release_task = releaser();

    loop.schedule(guarded_task);
    loop.schedule(cancel_task);
    loop.schedule(release_task);
    loop.run();

    auto result = guarded_task.value();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(started, 1);
    EXPECT_EQ(finished, 0);
}

TEST_CASE(destructor_cancels_tokens) {
    std::optional<cancellation_source> source(std::in_place);
    auto token = source->token();
    EXPECT_FALSE(token.cancelled());
    source.reset();
    EXPECT_TRUE(token.cancelled());
}

TEST_CASE(token_share_state) {
    cancellation_source source;
    auto token_a = source.token();
    auto token_b = token_a;

    EXPECT_FALSE(token_a.cancelled());
    EXPECT_FALSE(token_b.cancelled());

    source.cancel();
    EXPECT_TRUE(token_a.cancelled());
    EXPECT_TRUE(token_b.cancelled());
}

TEST_CASE(queue_cancel_resume) {
    event_loop loop;
    cancellation_source source;
    event start_target;
    event target_submitted;
    event target_done;

    const int pool_size = uv_thread_pool_size_for_test();
    const int blocker_count = pool_size + 1;
    std::atomic<int> blockers_started{0};
    std::atomic<int> blockers_done{0};
    std::atomic<bool> release{false};
    std::atomic<bool> target_started{false};

    int phase = 0;
    int observed_phase = 0;
    bool target_cancelled = false;

    auto blocker = [&]() -> task<> {
        auto ec = co_await queue(
            [&] {
                blockers_started.fetch_add(1, std::memory_order_relaxed);
                while(!release.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
                }
            },
            loop);
        EXPECT_FALSE(static_cast<bool>(ec));
        blockers_done.fetch_add(1, std::memory_order_release);
    };

    auto target = [&]() -> task<> {
        co_await start_target.wait();
        target_submitted.set();
        auto res = co_await with_token(
            queue([&] { target_started.store(true, std::memory_order_release); }, loop),
            source.token());
        target_cancelled = !res.has_value();
        observed_phase = phase;
        target_done.set();
    };

    auto canceler = [&]() -> task<> {
        while(blockers_started.load(std::memory_order_acquire) < pool_size) {
            co_await sleep(std::chrono::milliseconds{1}, loop);
        }

        start_target.set();
        co_await target_submitted.wait();

        phase = 1;
        source.cancel();
        phase = 2;

        release.store(true, std::memory_order_release);

        co_await target_done.wait();
        while(blockers_done.load(std::memory_order_acquire) < blocker_count) {
            co_await sleep(std::chrono::milliseconds{1}, loop);
        }

        loop.stop();
    };

    std::vector<task<>> blockers;
    blockers.reserve(static_cast<std::size_t>(blocker_count));
    for(int i = 0; i < blocker_count; ++i) {
        blockers.push_back(blocker());
    }

    auto target_task = target();
    auto cancel_task = canceler();

    for(auto& b: blockers) {
        loop.schedule(b);
    }
    loop.schedule(target_task);
    loop.schedule(cancel_task);
    loop.run();

    EXPECT_TRUE(target_cancelled);
    // Event-based cancellation is synchronous: the target resumes within
    // source.cancel(), before phase is advanced to 2.
    EXPECT_EQ(observed_phase, 1);
    EXPECT_FALSE(target_started.load(std::memory_order_acquire));
}

TEST_CASE(fs_cancel_resume) {
    event_loop loop;
    cancellation_source source;
    event start_target;
    event target_submitted;
    event target_done;

    const int pool_size = uv_thread_pool_size_for_test();
    const int blocker_count = pool_size + 1;
    std::atomic<int> blockers_started{0};
    std::atomic<int> blockers_done{0};
    std::atomic<bool> release{false};

    int phase = 0;
    int observed_phase = 0;
    bool target_cancelled = false;

    auto blocker = [&]() -> task<> {
        auto ec = co_await queue(
            [&] {
                blockers_started.fetch_add(1, std::memory_order_relaxed);
                while(!release.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
                }
            },
            loop);
        EXPECT_FALSE(static_cast<bool>(ec));
        blockers_done.fetch_add(1, std::memory_order_release);
    };

    auto target = [&]() -> task<> {
        co_await start_target.wait();
        target_submitted.set();
        auto res = co_await with_token(fs::stat(".", loop), source.token());
        target_cancelled = !res.has_value();
        observed_phase = phase;
        target_done.set();
    };

    auto canceler = [&]() -> task<> {
        while(blockers_started.load(std::memory_order_acquire) < pool_size) {
            co_await sleep(std::chrono::milliseconds{1}, loop);
        }

        start_target.set();
        co_await target_submitted.wait();

        phase = 1;
        source.cancel();
        phase = 2;

        release.store(true, std::memory_order_release);

        co_await target_done.wait();
        while(blockers_done.load(std::memory_order_acquire) < blocker_count) {
            co_await sleep(std::chrono::milliseconds{1}, loop);
        }

        loop.stop();
    };

    std::vector<task<>> blockers;
    blockers.reserve(static_cast<std::size_t>(blocker_count));
    for(int i = 0; i < blocker_count; ++i) {
        blockers.push_back(blocker());
    }

    auto target_task = target();
    auto cancel_task = canceler();

    for(auto& b: blockers) {
        loop.schedule(b);
    }
    loop.schedule(target_task);
    loop.schedule(cancel_task);
    loop.run();

    EXPECT_TRUE(target_cancelled);
    EXPECT_EQ(observed_phase, 1);
}

TEST_CASE(cancel_waiting_on_event) {
    event_loop loop;
    cancellation_source source;
    event gate;
    bool started = false;
    bool finished = false;

    auto worker = [&]() -> task<int> {
        started = true;
        co_await gate.wait();
        finished = true;
        co_return 42;
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        source.cancel();
    };

    auto guarded = with_token(worker(), source.token());
    auto cancel_task = canceler();

    loop.schedule(guarded);
    loop.schedule(cancel_task);
    loop.run();

    EXPECT_TRUE(started);
    EXPECT_FALSE(finished);
    EXPECT_FALSE(guarded.value().has_value());

    // Event remains usable after cancellation
    gate.set();
    EXPECT_TRUE(gate.is_set());
}

TEST_CASE(wait_sync_primitive) {
    event gate;

    auto worker = [&]() -> task<> {
        co_await gate.wait();
    };

    auto blocked = worker();
    blocked->resume();

    auto waiting_dot = blocked->dump_dot();
    EXPECT_NE(waiting_dot.find("Task"), std::string::npos);
    EXPECT_NE(waiting_dot.find("EventWaiter"), std::string::npos);
    EXPECT_NE(waiting_dot.find("Event"), std::string::npos);

    blocked->cancel();

    auto cancelled_dot = blocked->dump_dot();
    EXPECT_EQ(cancelled_dot.find("EventWaiter"), std::string::npos);

    gate.set();
    EXPECT_TRUE(gate.is_set());
}

TEST_CASE(cancel_waiting_on_mutex) {
    event_loop loop;
    cancellation_source source;
    mutex m;
    bool started = false;
    bool acquired = false;

    auto holder = [&]() -> task<> {
        co_await m.lock();
        co_await sleep(std::chrono::milliseconds{5}, loop);
        m.unlock();
    };

    auto worker = [&]() -> task<int> {
        started = true;
        co_await m.lock();
        acquired = true;
        m.unlock();
        co_return 1;
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        source.cancel();
    };

    auto holder_task = holder();
    auto guarded = with_token(worker(), source.token());
    auto cancel_task = canceler();

    loop.schedule(holder_task);
    loop.schedule(guarded);
    loop.schedule(cancel_task);
    loop.run();

    EXPECT_TRUE(started);
    EXPECT_FALSE(acquired);
    EXPECT_FALSE(guarded.value().has_value());

    // Mutex remains functional after cancellation
    EXPECT_TRUE(m.try_lock());
    m.unlock();
}

TEST_CASE(cancel_semaphore_waiter) {
    event_loop loop;
    cancellation_source source;
    semaphore sem(0);
    bool started = false;
    bool acquired = false;

    auto worker = [&]() -> task<int> {
        started = true;
        co_await sem.acquire();
        acquired = true;
        co_return 1;
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        source.cancel();
    };

    auto guarded = with_token(worker(), source.token());
    auto cancel_task = canceler();

    loop.schedule(guarded);
    loop.schedule(cancel_task);
    loop.run();

    EXPECT_TRUE(started);
    EXPECT_FALSE(acquired);
    EXPECT_FALSE(guarded.value().has_value());

    // Semaphore remains usable
    sem.release();
    EXPECT_TRUE(sem.try_acquire());
}

TEST_CASE(cancel_condition_variable_waiter) {
    event_loop loop;
    cancellation_source source;
    mutex m;
    condition_variable cv;
    bool started = false;
    bool notified = false;

    auto worker = [&]() -> task<int> {
        started = true;
        co_await m.lock();
        co_await cv.wait(m);
        notified = true;
        m.unlock();
        co_return 1;
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        source.cancel();
    };

    auto guarded = with_token(worker(), source.token());
    auto cancel_task = canceler();

    loop.schedule(guarded);
    loop.schedule(cancel_task);
    loop.run();

    EXPECT_TRUE(started);
    EXPECT_FALSE(notified);
    EXPECT_FALSE(guarded.value().has_value());
}

TEST_CASE(cancel_multiple_registered_tasks) {
    event_loop loop;
    cancellation_source source;
    event gate1, gate2, gate3;
    int started = 0;
    int finished = 0;

    auto make_worker = [&](event& gate) -> task<int> {
        started += 1;
        co_await gate.wait();
        finished += 1;
        co_return 1;
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        source.cancel();
    };

    auto token = source.token();
    auto g1 = with_token(make_worker(gate1), token);
    auto g2 = with_token(make_worker(gate2), token);
    auto g3 = with_token(make_worker(gate3), token);
    auto cancel_task = canceler();

    loop.schedule(g1);
    loop.schedule(g2);
    loop.schedule(g3);
    loop.schedule(cancel_task);
    loop.run();

    EXPECT_EQ(started, 3);
    EXPECT_EQ(finished, 0);
    EXPECT_FALSE(g1.value().has_value());
    EXPECT_FALSE(g2.value().has_value());
    EXPECT_FALSE(g3.value().has_value());
}

TEST_CASE(nested_with_token) {
    // (a) Cancel outer -> entire chain cancelled
    {
        event_loop loop;
        cancellation_source outer_source;
        cancellation_source inner_source;
        event gate;

        auto worker = [&]() -> task<int> {
            co_await gate.wait();
            co_return 42;
        };

        auto canceler = [&]() -> task<> {
            co_await sleep(std::chrono::milliseconds{1}, loop);
            outer_source.cancel();
        };

        auto guarded = with_token(with_token(worker(), inner_source.token()), outer_source.token());
        auto cancel_task = canceler();

        loop.schedule(guarded);
        loop.schedule(cancel_task);
        loop.run();

        EXPECT_FALSE(guarded.value().has_value());
    }

    // (b) Cancel inner -> inner task reports cancellation, outer observes it
    {
        event_loop loop;
        cancellation_source outer_source;
        cancellation_source inner_source;
        event gate;

        auto worker = [&]() -> task<int> {
            co_await gate.wait();
            co_return 42;
        };

        auto canceler = [&]() -> task<> {
            co_await sleep(std::chrono::milliseconds{1}, loop);
            inner_source.cancel();
        };

        auto guarded = with_token(with_token(worker(), inner_source.token()), outer_source.token());
        auto cancel_task = canceler();

        loop.schedule(guarded);
        loop.schedule(cancel_task);
        loop.run();

        // Outer observes cancellation result from inner
        EXPECT_FALSE(guarded.value().has_value());
        // But the outer source itself was NOT cancelled
        EXPECT_FALSE(outer_source.cancelled());
    }
}

TEST_CASE(token_reuse_after_cancel) {
    cancellation_source source;
    source.cancel();

    int started = 0;
    auto worker = [&]() -> task<int> {
        started += 1;
        co_return 1;
    };

    // Create new task with already-cancelled token
    auto [result] = run(with_token(worker(), source.token()));
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(started, 0);

    // registration.cancelled() returns true
    EXPECT_TRUE(source.cancelled());
}

TEST_CASE(multi_token_cancel_first) {
    event_loop loop;
    cancellation_source source1;
    cancellation_source source2;
    event gate;
    bool finished = false;

    auto worker = [&]() -> task<int> {
        co_await gate.wait();
        finished = true;
        co_return 42;
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        source1.cancel();
    };

    auto guarded = with_token(worker(), source1.token(), source2.token());
    auto cancel_task = canceler();

    loop.schedule(guarded);
    loop.schedule(cancel_task);
    loop.run();

    EXPECT_FALSE(finished);
    EXPECT_FALSE(guarded.value().has_value());
    EXPECT_TRUE(source1.cancelled());
    EXPECT_FALSE(source2.cancelled());
}

TEST_CASE(multi_token_cancel_second) {
    event_loop loop;
    cancellation_source source1;
    cancellation_source source2;
    event gate;
    bool finished = false;

    auto worker = [&]() -> task<int> {
        co_await gate.wait();
        finished = true;
        co_return 42;
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        source2.cancel();
    };

    auto guarded = with_token(worker(), source1.token(), source2.token());
    auto cancel_task = canceler();

    loop.schedule(guarded);
    loop.schedule(cancel_task);
    loop.run();

    EXPECT_FALSE(finished);
    EXPECT_FALSE(guarded.value().has_value());
    EXPECT_FALSE(source1.cancelled());
    EXPECT_TRUE(source2.cancelled());
}

TEST_CASE(multi_token_pre_cancel) {
    cancellation_source source1;
    cancellation_source source2;
    source2.cancel();

    int started = 0;
    auto worker = [&]() -> task<int> {
        started += 1;
        co_return 1;
    };

    auto [result] = run(with_token(worker(), source1.token(), source2.token()));
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(started, 0);
}

TEST_CASE(multi_token_pass_through) {
    cancellation_source source1;
    cancellation_source source2;

    auto worker = []() -> task<int> {
        co_return 99;
    };

    auto [result] = run(with_token(worker(), source1.token(), source2.token()));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 99);
}

TEST_CASE(nested_with_token_same_token_cancel) {
    event_loop loop;
    cancellation_source source;
    auto token = source.token();
    event gate;
    event inner_started;

    auto inner_work = [&]() -> task<> {
        inner_started.set();
        co_await gate.wait();
    };

    auto outer_work = [&](cancellation_token t) -> task<> {
        co_await with_token(inner_work(), t);
    };

    auto guarded = with_token(outer_work(token), token);

    auto canceler = [&]() -> task<> {
        co_await inner_started.wait();
        source.cancel();
        co_return;
    };

    loop.schedule(guarded);
    loop.schedule(canceler());
    EXPECT_EQ(loop.run(), 0);
}

};  // TEST_SUITE(cancellation)

}  // namespace

}  // namespace eventide
