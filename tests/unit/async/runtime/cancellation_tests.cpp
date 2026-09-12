// with_token cancellation: pre-cancel skip, cancel-in-flight, token sharing,
// cancelling tasks blocked on sync primitives (event/mutex/semaphore/cv),
// multi-token / nested with_token, and cancellation checkpoints. Threadpool
// cancel races (queue/fs) live here too. Aggregate cancel semantics live in
// when/cancel.cpp; task_group cancel in task_group/cancel.cpp.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <semaphore>
#include <thread>
#include <vector>

#include "../loop_fixture.h"
#include "kota/zest/zest.h"

namespace kota {

namespace {

int uv_thread_pool_size_for_test() {
    int value = 4;
#ifdef _WIN32
    char* raw = nullptr;
    std::size_t raw_size = 0;
    if(_dupenv_s(&raw, &raw_size, "UV_THREADPOOL_SIZE") == 0 && raw != nullptr) {
        int parsed = std::atoi(raw);
        std::free(raw);
        if(parsed > 0) {
            value = parsed;
        }
    }
#else
    if(const char* raw = std::getenv("UV_THREADPOOL_SIZE"); raw != nullptr) {
        int parsed = std::atoi(raw);
        if(parsed > 0) {
            value = parsed;
        }
    }
#endif

    return value;
}

TEST_SUITE(cancellation, loop_fixture) {

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
        co_await sleep(1, loop);
        source.cancel();
    };

    auto releaser = [&]() -> task<> {
        co_await sleep(2, loop);
        gate.set();
    };

    auto guarded_task = with_token(worker(), source.token());
    auto cancel_task = canceler();
    auto release_task = releaser();
    schedule_all(guarded_task, cancel_task, release_task);

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

TEST_CASE(queue_cancel_resume, serial = true) {
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
        EXPECT_FALSE(ec.has_error());
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
            co_await sleep(1, loop);
        }

        start_target.set();
        co_await target_submitted.wait();

        phase = 1;
        source.cancel();
        phase = 2;

        // cancel() only schedules the token waiter; the cascade that reaches
        // work_op::on_cancel (and thus uv_cancel) runs in the loop's check
        // phase. Wait for the target to settle before releasing the blockers,
        // otherwise a freed pool thread races uv_cancel for the queued target
        // and can spuriously start it.
        co_await target_done.wait();

        release.store(true, std::memory_order_release);

        while(blockers_done.load(std::memory_order_acquire) < blocker_count) {
            co_await sleep(1, loop);
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
    // Structured completion: with_token uses when_any internally, which
    // now waits for all children (including the cancelled inner task).
    // The target resumes after source.cancel() returns and the event
    // loop completes the cancellation, so phase has already advanced to 2.
    EXPECT_EQ(observed_phase, 2);
    EXPECT_FALSE(target_started.load(std::memory_order_acquire));
}

TEST_CASE(queue_cancel_hook_signals_running_work, serial = true) {
    cancellation_source source;
    std::atomic<bool> started{false};
    std::atomic<bool> stop_flag{false};
    std::atomic<bool> observed_stop{false};
    bool cancelled = false;

    auto target = [&]() -> task<> {
        auto res = co_await with_token(
            queue(
                [&] {
                    started.store(true, std::memory_order_release);
                    while(!stop_flag.load(std::memory_order_acquire)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds{1});
                    }
                    observed_stop.store(true, std::memory_order_release);
                },
                [&] { stop_flag.store(true, std::memory_order_release); },
                loop),
            source.token());
        cancelled = res.is_cancelled();
        loop.stop();
    };

    auto canceler = [&]() -> task<> {
        while(!started.load(std::memory_order_acquire)) {
            co_await sleep(1, loop);
        }
        source.cancel();
    };

    auto target_task = target();
    auto cancel_task = canceler();
    loop.schedule(target_task);
    loop.schedule(cancel_task);
    loop.run();

    // The work was already running when the token fired: uv_cancel can't
    // dequeue it, so the on_cancel hook is the only way it returns.
    EXPECT_TRUE(cancelled);
    EXPECT_TRUE(observed_stop.load(std::memory_order_acquire));
}

TEST_CASE(queue_cancel_hook_runs_on_loop_thread, serial = true) {
    cancellation_source source;
    std::atomic<bool> started{false};
    std::atomic<bool> hook_on_loop_thread{false};
    std::binary_semaphore wakeup{0};
    bool cancelled = false;

    const auto loop_thread = std::this_thread::get_id();

    auto target = [&]() -> task<> {
        auto res = co_await with_token(queue(
                                           [&] {
                                               started.store(true, std::memory_order_release);
                                               wakeup.acquire();
                                           },
                                           [&] {
                                               hook_on_loop_thread.store(
                                                   std::this_thread::get_id() == loop_thread,
                                                   std::memory_order_release);
                                               wakeup.release();
                                           },
                                           loop),
                                       source.token());
        cancelled = res.is_cancelled();
        loop.stop();
    };

    auto canceler = [&]() -> task<> {
        while(!started.load(std::memory_order_acquire)) {
            co_await sleep(1, loop);
        }
        source.cancel();
    };

    auto target_task = target();
    auto cancel_task = canceler();
    loop.schedule(target_task);
    loop.schedule(cancel_task);
    loop.run();

    // Blocking-style work: the fn sleeps on a semaphore that only the hook
    // releases, and the hook must run on the loop thread.
    EXPECT_TRUE(cancelled);
    EXPECT_TRUE(hook_on_loop_thread.load(std::memory_order_acquire));
}

TEST_CASE(fs_cancel_resume, serial = true) {
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
        EXPECT_FALSE(ec.has_error());
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
            co_await sleep(1, loop);
        }

        start_target.set();
        co_await target_submitted.wait();

        phase = 1;
        source.cancel();
        phase = 2;

        release.store(true, std::memory_order_release);

        co_await target_done.wait();
        while(blockers_done.load(std::memory_order_acquire) < blocker_count) {
            co_await sleep(1, loop);
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
    // Structured completion: target resumes after the cancelled fs::stat
    // completes via the event loop, so phase has already advanced to 2.
    EXPECT_EQ(observed_phase, 2);
}

TEST_CASE(cancel_waiting_on_event) {
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
        co_await sleep(1, loop);
        source.cancel();
    };

    auto guarded = with_token(worker(), source.token());
    auto cancel_task = canceler();
    schedule_all(guarded, cancel_task);

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

    auto waiting_dot = dump_dot(blocked);
    EXPECT_NE(waiting_dot.find("Task"), std::string::npos);
    EXPECT_NE(waiting_dot.find("EventWaiter"), std::string::npos);
    EXPECT_NE(waiting_dot.find("Event"), std::string::npos);

    blocked->cancel();

    auto cancelled_dot = dump_dot(blocked);
    EXPECT_EQ(cancelled_dot.find("EventWaiter"), std::string::npos);

    gate.set();
    EXPECT_TRUE(gate.is_set());
}

TEST_CASE(cancel_waiting_on_mutex) {
    cancellation_source source;
    mutex m;
    bool started = false;
    bool acquired = false;

    auto holder = [&]() -> task<> {
        co_await m.lock();
        co_await sleep(5, loop);
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
        co_await sleep(1, loop);
        source.cancel();
    };

    auto holder_task = holder();
    auto guarded = with_token(worker(), source.token());
    auto cancel_task = canceler();
    schedule_all(holder_task, guarded, cancel_task);

    EXPECT_TRUE(started);
    EXPECT_FALSE(acquired);
    EXPECT_FALSE(guarded.value().has_value());

    // Mutex remains functional after cancellation
    EXPECT_TRUE(m.try_lock());
    m.unlock();
}

TEST_CASE(cancel_releases_scoped_mutex) {
    cancellation_source source;
    mutex m;

    auto holder = [&]() -> task<int> {
        auto lock = co_await m.scoped();
        co_await sleep(50, loop);
        co_return 1;
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(1, loop);
        source.cancel();
    };

    {
        auto guarded = with_token(holder(), source.token());
        auto cancel_task = canceler();
        schedule_all(guarded, cancel_task);
        EXPECT_FALSE(guarded.value().has_value());
    }

    // Destroying the cancelled task destroys the holder frame; the scoped
    // guard in that frame releases the mutex (a manual lock()/unlock() pair
    // would leak the lock here, since unlock() is never reached).
    EXPECT_TRUE(m.try_lock());
    m.unlock();
}

TEST_CASE(cancel_semaphore_waiter) {
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
        co_await sleep(1, loop);
        source.cancel();
    };

    auto guarded = with_token(worker(), source.token());
    auto cancel_task = canceler();
    schedule_all(guarded, cancel_task);

    EXPECT_TRUE(started);
    EXPECT_FALSE(acquired);
    EXPECT_FALSE(guarded.value().has_value());

    // Semaphore remains usable
    sem.release();
    EXPECT_TRUE(sem.try_acquire());
}

TEST_CASE(cancel_condition_variable_waiter) {
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
        co_await sleep(1, loop);
        source.cancel();
    };

    auto guarded = with_token(worker(), source.token());
    auto cancel_task = canceler();
    schedule_all(guarded, cancel_task);

    EXPECT_TRUE(started);
    EXPECT_FALSE(notified);
    EXPECT_FALSE(guarded.value().has_value());
}

TEST_CASE(cancel_multiple_registered_tasks) {
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
        co_await sleep(1, loop);
        source.cancel();
    };

    auto token = source.token();
    auto g1 = with_token(make_worker(gate1), token);
    auto g2 = with_token(make_worker(gate2), token);
    auto g3 = with_token(make_worker(gate3), token);
    auto cancel_task = canceler();
    schedule_all(g1, g2, g3, cancel_task);

    EXPECT_EQ(started, 3);
    EXPECT_EQ(finished, 0);
    EXPECT_FALSE(g1.value().has_value());
    EXPECT_FALSE(g2.value().has_value());
    EXPECT_FALSE(g3.value().has_value());
}

TEST_CASE(nested_with_token) {
    // (a) Cancel outer -> entire chain cancelled
    {
        cancellation_source outer_source;
        cancellation_source inner_source;
        event gate;

        auto worker = [&]() -> task<int> {
            co_await gate.wait();
            co_return 42;
        };

        auto canceler = [&]() -> task<> {
            co_await sleep(1, loop);
            outer_source.cancel();
        };

        auto guarded = with_token(with_token(worker(), inner_source.token()), outer_source.token());
        auto cancel_task = canceler();
        schedule_all(guarded, cancel_task);

        EXPECT_FALSE(guarded.value().has_value());
    }

    // (b) Cancel inner -> inner task reports cancellation, outer observes it
    {
        cancellation_source outer_source;
        cancellation_source inner_source;
        event gate;

        auto worker = [&]() -> task<int> {
            co_await gate.wait();
            co_return 42;
        };

        auto canceler = [&]() -> task<> {
            co_await sleep(1, loop);
            inner_source.cancel();
        };

        auto guarded = with_token(with_token(worker(), inner_source.token()), outer_source.token());
        auto cancel_task = canceler();
        schedule_all(guarded, cancel_task);

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
        co_await sleep(1, loop);
        source1.cancel();
    };

    auto guarded = with_token(worker(), source1.token(), source2.token());
    auto cancel_task = canceler();
    schedule_all(guarded, cancel_task);

    EXPECT_FALSE(finished);
    EXPECT_FALSE(guarded.value().has_value());
    EXPECT_TRUE(source1.cancelled());
    EXPECT_FALSE(source2.cancelled());
}

TEST_CASE(multi_token_cancel_second) {
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
        co_await sleep(1, loop);
        source2.cancel();
    };

    auto guarded = with_token(worker(), source1.token(), source2.token());
    auto cancel_task = canceler();
    schedule_all(guarded, cancel_task);

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

    auto cancel_task = canceler();
    schedule_all(guarded, cancel_task);
}

// Exercises the void-returning with_token cancellation path — the MSVC
// coroutine codegen can fall through past co_await cancel() and reach
// undefined behavior when dereferencing a cancelled race_result.
TEST_CASE(cancel_void_task_in_flight) {
    cancellation_source source;
    event gate;
    bool started = false;
    bool finished = false;

    auto worker = [&]() -> task<> {
        started = true;
        co_await gate.wait();
        finished = true;
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(1, loop);
        source.cancel();
    };

    auto guarded = with_token(worker(), source.token());
    auto cancel_task = canceler();
    schedule_all(guarded, cancel_task);

    EXPECT_TRUE(started);
    EXPECT_FALSE(finished);
    EXPECT_TRUE(source.cancelled());
    EXPECT_TRUE(guarded.result().is_cancelled());
}

// ============================================================================
// Cancellation checkpoints: a task cancelled while executing observes the
// cancellation at its next suspending co_await instead of starting new work
// (trio-style prompt cancellation).
// ============================================================================

// Pure-compute awaits (no event loop round trip) are checkpoints too.
// Before checkpoint semantics this loop ran all 100 iterations.
TEST_CASE(checkpoint_stops_pure_compute_loop) {
    int loop_count = 0;
    async_node* worker_node = nullptr;

    auto compute = []() -> task<int> {
        co_return 1;
    };

    auto worker = [&]() -> task<> {
        for(int i = 0; i < 100; ++i) {
            if(i == 3) {
                worker_node->cancel();
            }
            (void)co_await compute();
            loop_count = i + 1;
        }
    };

    auto t = worker();
    worker_node = t.operator->();
    schedule_all(t);

    EXPECT_EQ(loop_count, 3);
    EXPECT_TRUE(t->is_cancelled());
}

// Awaiting an aggregate under a cancelled parent must not start any child.
TEST_CASE(checkpoint_skips_when_all_children) {
    bool child_ran = false;
    async_node* worker_node = nullptr;

    auto child = [&]() -> task<> {
        child_ran = true;
        co_return;
    };

    auto worker = [&]() -> task<> {
        worker_node->cancel();
        co_await when_all(child(), child());
    };

    auto t = worker();
    worker_node = t.operator->();
    schedule_all(t);

    EXPECT_FALSE(child_ran);
    EXPECT_TRUE(t->is_cancelled());
}

// A cancelled task never enters a sync primitive's wait queue.
TEST_CASE(checkpoint_skips_event_wait) {
    event ev;
    async_node* worker_node = nullptr;

    auto worker = [&]() -> task<> {
        worker_node->cancel();
        (void)co_await event::wait_awaiter(ev);
    };

    auto t = worker();
    worker_node = t.operator->();
    schedule_all(t);

    EXPECT_TRUE(t->is_cancelled());
    EXPECT_EQ(ev.get_head(), nullptr);
}

// join() under a cancelled parent cancels the group's children and waits for
// them to complete (structured completion) before propagating the cancel.
TEST_CASE(checkpoint_join_cancels_group) {
    int slow_done = 0;
    async_node* worker_node = nullptr;

    auto slow = [&]() -> task<> {
        co_await sleep(std::chrono::seconds(10), loop);
        slow_done += 1;
    };

    auto worker = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(slow());
        worker_node->cancel();
        co_await group.join();
    };

    auto t = worker();
    worker_node = t.operator->();
    schedule_all(t);

    EXPECT_TRUE(t->is_cancelled());
    EXPECT_EQ(slow_done, 0);
}

};  // TEST_SUITE(cancellation)

}  // namespace

}  // namespace kota
