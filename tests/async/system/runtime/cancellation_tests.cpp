// with_token cancellation: pre-cancel skip, cancel-in-flight, token sharing,
// cancelling tasks blocked on sync primitives (event/mutex/semaphore/cv),
// multi-token / nested with_token, and cancellation checkpoints. Threadpool
// cancel races (queue/fs) live here too. Aggregate cancel semantics live in
// when/cancel_tests.cpp; task_group cancel in task_group/cancel_tests.cpp.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <semaphore>
#include <thread>
#include <vector>

#include "async/harness/loop_fixture.h"
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

ZEST_SUITE(async_runtime_cancellation, loop_fixture) {

ZEST_CASE(queue_cancel_resume) {
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
        EXPECT(!ec.has_error());
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

    EXPECT(target_cancelled);
    // Structured completion: with_token uses when_any internally, which
    // now waits for all children (including the cancelled inner task).
    // The target resumes after source.cancel() returns and the event
    // loop completes the cancellation, so phase has already advanced to 2.
    EXPECT(observed_phase == 2);
    EXPECT(!target_started.load(std::memory_order_acquire));
}

ZEST_CASE(queue_cancel_hook_signals_running_work) {
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
    EXPECT(cancelled);
    EXPECT(observed_stop.load(std::memory_order_acquire));
}

ZEST_CASE(queue_cancel_hook_runs_on_loop_thread) {
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
    EXPECT(cancelled);
    EXPECT(hook_on_loop_thread.load(std::memory_order_acquire));
}

ZEST_CASE(fs_cancel_resume) {
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
        EXPECT(!ec.has_error());
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

        // As in queue_cancel_resume: a pool thread freed before the target
        // settles races uv_cancel for it.
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

    EXPECT(target_cancelled);
    // Structured completion: target resumes after the cancelled fs::stat
    // completes via the event loop, so phase has already advanced to 2.
    EXPECT(observed_phase == 2);
}

};  // ZEST_SUITE(async_runtime_cancellation)

}  // namespace

}  // namespace kota
