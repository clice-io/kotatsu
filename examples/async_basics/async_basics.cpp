/// async_basics.cpp — Showcases eventide's async primitives.
///
/// Each section is self-contained. Run the program to see all examples
/// execute sequentially, each printing its output.

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "eventide/async/cancellation.h"
#include "eventide/async/loop.h"
#include "eventide/async/sync.h"
#include "eventide/async/task.h"
#include "eventide/async/watcher.h"
#include "eventide/async/when.h"

using namespace eventide;
using namespace std::chrono_literals;

// ============================================================
// 1. Basic tasks — creation, chaining, and run()
// ============================================================

task<int> add(int a, int b) {
    co_return a + b;
}

task<int> compute() {
    auto x = co_await add(1, 2);
    auto y = co_await add(x, 10);
    co_return y;
}

void example_basic_tasks() {
    std::printf("--- 1. Basic tasks ---\n");

    // run() creates a temporary event loop, schedules the task, runs it,
    // and returns the result wrapped in a tuple.
    auto [result] = run(compute());
    std::printf("compute() = %d\n\n", result);
}

// ============================================================
// 2. Timers and sleep — async delays
// ============================================================

task<> timed_greeting(event_loop& loop) {
    std::printf("  (waiting 50ms...)\n");
    co_await sleep(50ms, loop);
    std::printf("  Hello after 50ms!\n");
}

void example_timers() {
    std::printf("--- 2. Timers and sleep ---\n");

    event_loop loop;
    auto t = timed_greeting(loop);
    loop.schedule(t);
    loop.run();
    std::printf("\n");
}

// ============================================================
// 3. when_all — run tasks concurrently, collect all results
// ============================================================

task<int> slow_add(int a, int b, event_loop& loop) {
    co_await sleep(10ms, loop);
    co_return a + b;
}

void example_when_all() {
    std::printf("--- 3. when_all ---\n");

    event_loop loop;

    auto combined = [&]() -> task<int> {
        // Both additions run concurrently. We wait for BOTH to finish.
        auto [x, y] = co_await when_all(slow_add(1, 2, loop), slow_add(10, 20, loop));
        co_return x + y;
    };

    auto t = combined();
    loop.schedule(t);
    loop.run();

    std::printf("when_all result = %d\n\n", t.result());
}

// ============================================================
// 4. when_any — race tasks, first one wins
// ============================================================

task<std::string> fetch(const char* name, int delay_ms, event_loop& loop) {
    co_await sleep(std::chrono::milliseconds{delay_ms}, loop);
    co_return std::string(name);
}

void example_when_any() {
    std::printf("--- 4. when_any ---\n");

    event_loop loop;

    auto race = [&]() -> task<std::size_t> {
        // The fastest task wins. All others are cancelled.
        auto winner = co_await when_any(fetch("slow-server", 100, loop),
                                        fetch("fast-server", 10, loop),
                                        fetch("medium-server", 50, loop));
        co_return winner;
    };

    auto t = race();
    loop.schedule(t);
    loop.run();

    std::printf("winner index = %zu (fast-server)\n\n", t.result());
}

// ============================================================
// 5. async_scope — dynamic structured concurrency
// ============================================================

void example_async_scope() {
    std::printf("--- 5. async_scope ---\n");

    event_loop loop;
    int total = 0;

    auto worker = [&](int id, int value) -> task<> {
        co_await sleep(std::chrono::milliseconds{id * 5}, loop);
        total += value;
        std::printf("  worker %d finished (added %d)\n", id, value);
    };

    auto driver = [&]() -> task<> {
        async_scope scope;

        // Spawn a dynamic number of tasks at runtime.
        for(int i = 0; i < 5; ++i) {
            scope.spawn(worker(i, (i + 1) * 10));
        }

        // Wait for all spawned tasks to complete.
        co_await scope;
        std::printf("  all workers done, total = %d\n", total);
    };

    auto t = driver();
    loop.schedule(t);
    loop.run();
    std::printf("\n");
}

// ============================================================
// 6. Cancellation — cooperative cancel and catch_cancel
// ============================================================

void example_cancellation() {
    std::printf("--- 6. Cancellation ---\n");

    event_loop loop;

    // 6a. Self-cancellation with co_await cancel()
    {
        auto self_cancel = []() -> task<int> {
            co_await cancel();
            co_return 42;  // never reached
        };

        auto handler = [&]() -> task<> {
            // catch_cancel() converts task<int> -> ctask<int>
            // (= task<expected<int, cancellation>>).
            // Cancellation becomes a value instead of propagating.
            auto result = co_await self_cancel().catch_cancel();
            if(result.has_value()) {
                std::printf("  got value: %d\n", *result);
            } else {
                std::printf("  6a: caught cancellation (expected)\n");
            }
        };

        run(handler());
    }

    // 6b. External cancellation with cancellation_token
    {
        cancellation_source source;
        int started = 0, finished = 0;

        auto slow_work = [&]() -> task<int> {
            started += 1;
            co_await sleep(100ms, loop);
            finished += 1;
            co_return 1;
        };

        // with_token wraps a task so it can be cancelled externally.
        auto guarded = with_token(source.token(), slow_work());

        auto canceler = [&]() -> task<> {
            co_await sleep(10ms, loop);
            source.cancel();  // cancels guarded mid-flight
        };

        auto cancel_task = canceler();
        loop.schedule(guarded);
        loop.schedule(cancel_task);
        loop.run();

        std::printf("  6b: started=%d, finished=%d, cancelled=%s\n",
                    started,
                    finished,
                    guarded.value().has_value() ? "no" : "yes");
    }

    std::printf("\n");
}

// ============================================================
// 7. Sync primitives — mutex and event
// ============================================================

void example_sync_primitives() {
    std::printf("--- 7. Sync primitives ---\n");

    event_loop loop;

    // 7a. Mutex — serialize access to shared state
    {
        mutex m;
        std::string log;

        auto append = [&](const char* msg, int delay_ms) -> task<> {
            co_await m.lock();
            co_await sleep(std::chrono::milliseconds{delay_ms}, loop);
            log += msg;
            m.unlock();
        };

        auto driver = [&]() -> task<> {
            co_await when_all(append("A", 5), append("B", 1), append("C", 3));
        };

        auto t = driver();
        loop.schedule(t);
        loop.run();

        // Mutex ensures sequential access despite concurrent tasks.
        std::printf("  7a mutex log = \"%s\" (length 3, order depends on lock acquisition)\n",
                    log.c_str());
    }

    // 7b. Event — signal between tasks
    {
        event gate;
        bool producer_done = false;
        bool consumer_saw_it = false;

        auto producer = [&]() -> task<> {
            co_await sleep(10ms, loop);
            producer_done = true;
            gate.set();  // wake the consumer
        };

        auto consumer = [&]() -> task<> {
            co_await gate.wait();  // blocks until gate.set()
            consumer_saw_it = producer_done;
        };

        auto driver = [&]() -> task<> {
            co_await when_all(producer(), consumer());
        };

        auto t = driver();
        loop.schedule(t);
        loop.run();

        std::printf("  7b event: consumer_saw_it = %s\n", consumer_saw_it ? "true" : "false");
    }

    std::printf("\n");
}

// ============================================================
// 8. Combining patterns — scope + when_all + cancellation
// ============================================================

void example_combined() {
    std::printf("--- 8. Combined patterns ---\n");

    event_loop loop;

    // Use a scope to spawn workers, each doing a when_all internally,
    // with external cancellation cutting everything short.

    cancellation_source source;
    int completed_pairs = 0;

    auto pair_work = [&](int id) -> task<> {
        auto a = [&]() -> task<int> {
            co_await sleep(std::chrono::milliseconds{5 + id}, loop);
            co_return id * 10;
        };
        auto b = [&]() -> task<int> {
            co_await sleep(std::chrono::milliseconds{5 + id}, loop);
            co_return id * 20;
        };
        auto [x, y] = co_await when_all(a(), b());
        completed_pairs += 1;
        std::printf("  pair %d: %d + %d = %d\n", id, x, y, x + y);
    };

    auto driver = [&]() -> task<int> {
        async_scope scope;
        for(int i = 0; i < 5; ++i) {
            scope.spawn(pair_work(i));
        }
        co_await scope;
        co_return completed_pairs;
    };

    auto guarded = with_token(source.token(), driver());

    // Cancel after 12ms — some pairs will finish, others won't.
    auto canceler = [&]() -> task<> {
        co_await sleep(12ms, loop);
        source.cancel();
    };

    auto cancel_task = canceler();
    loop.schedule(guarded);
    loop.schedule(cancel_task);
    loop.run();

    auto result = guarded.value();
    if(result.has_value()) {
        std::printf("  all done: %d pairs completed\n", *result);
    } else {
        std::printf("  cancelled after %d pairs\n", completed_pairs);
    }
}

// ============================================================

int main() {
    std::printf("=== eventide async examples ===\n\n");

    example_basic_tasks();
    example_timers();
    example_when_all();
    example_when_any();
    example_async_scope();
    example_cancellation();
    example_sync_primitives();
    example_combined();

    std::printf("\n=== done ===\n");
    return 0;
}
