/// async_basics.cpp — Showcases kotatsu's async primitives.
///
/// Each section is self-contained. Run the program to see all examples
/// execute sequentially, each printing its output.

#include <chrono>
#include <print>
#include <string>
#include <vector>

#include "kota/async/async.h"

using namespace kota;
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
    std::println("--- 1. Basic tasks ---");

    // run() creates a temporary event loop, schedules the task, runs it,
    // and returns the result wrapped in a tuple.
    auto [result] = run(compute());
    std::println("compute() = {}", *result);
    std::println("");
}

// ============================================================
// 2. Timers and sleep — async delays
// ============================================================

task<> timed_greeting(event_loop& loop) {
    std::println("  (waiting 50ms...)");
    co_await sleep(50ms, loop);
    std::println("  Hello after 50ms!");
}

void example_timers() {
    std::println("--- 2. Timers and sleep ---");

    event_loop loop;
    auto t = timed_greeting(loop);
    loop.schedule(t);
    loop.run();
    std::println("");
}

// ============================================================
// 3. when_all — run tasks concurrently, collect all results
// ============================================================

task<int> slow_add(int a, int b, event_loop& loop) {
    co_await sleep(10ms, loop);
    co_return a + b;
}

void example_when_all() {
    std::println("--- 3. when_all ---");

    event_loop loop;

    auto combined = [&]() -> task<int> {
        // Both additions run concurrently. We wait for BOTH to finish.
        auto [x, y] = co_await when_all(slow_add(1, 2, loop), slow_add(10, 20, loop));
        co_return x + y;
    };

    auto t = combined();
    loop.schedule(t);
    loop.run();

    std::println("when_all result = {}", t.result());
    std::println("");
}

// ============================================================
// 4. when_any — race tasks, first one wins
// ============================================================

task<std::string> fetch(const char* name, int delay_ms, event_loop& loop) {
    co_await sleep(delay_ms, loop);
    co_return std::string(name);
}

void example_when_any() {
    std::println("--- 4. when_any ---");

    event_loop loop;

    auto race = [&]() -> task<std::variant<std::string, std::string, std::string>> {
        // The fastest task wins. All others are cancelled.
        co_return co_await when_any(fetch("slow-server", 100, loop),
                                    fetch("fast-server", 10, loop),
                                    fetch("medium-server", 50, loop));
    };

    auto t = race();
    loop.schedule(t);
    loop.run();

    auto winner = t.result();
    std::println("winner index = {} (fast-server), value = {}",
                 winner.index(),
                 std::get<1>(winner));
    std::println("");
}

// ============================================================
// 5. async_scope — dynamic structured concurrency
// ============================================================

void example_async_scope() {
    std::println("--- 5. async_scope ---");

    event_loop loop;
    int total = 0;

    auto worker = [&](int id, int value) -> task<> {
        co_await sleep(id * 5, loop);
        total += value;
        std::println("  worker {} finished (added {})", id, value);
    };

    auto driver = [&]() -> task<> {
        async_scope scope;

        // Spawn a dynamic number of tasks at runtime.
        for(int i = 0; i < 5; ++i) {
            scope.spawn(worker(i, (i + 1) * 10));
        }

        // Wait for all spawned tasks to complete.
        co_await scope;
        std::println("  all workers done, total = {}", total);
    };

    auto t = driver();
    loop.schedule(t);
    loop.run();
    std::println("");
}

// ============================================================
// 6. Cancellation — cooperative cancel and catch_cancel
// ============================================================

void example_cancellation() {
    std::println("--- 6. Cancellation ---");

    event_loop loop;

    // 6a. Self-cancellation with co_await cancel()
    {
        auto self_cancel = []() -> task<int> {
            co_await cancel();
            co_return 42;  // never reached
        };

        auto handler = [&]() -> task<> {
            // Cancellation becomes a value instead of propagating.
            auto result = co_await self_cancel().catch_cancel();
            if(result.has_value()) {
                std::println("  got value: {}", *result);
            } else {
                std::println("  6a: caught cancellation (expected)");
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
        auto guarded = with_token(slow_work(), source.token());

        auto canceler = [&]() -> task<> {
            co_await sleep(10ms, loop);
            source.cancel();  // cancels guarded mid-flight
        };

        auto cancel_task = canceler();
        loop.schedule(guarded);
        loop.schedule(cancel_task);
        loop.run();

        std::println("  6b: started={}, finished={}, cancelled={}",
                     started,
                     finished,
                     guarded.value().has_value() ? "no" : "yes");
    }

    std::println("");
}

// ============================================================
// 7. Sync primitives — mutex and event
// ============================================================

void example_sync_primitives() {
    std::println("--- 7. Sync primitives ---");

    event_loop loop;

    // 7a. Mutex — serialize access to shared state
    {
        mutex m;
        std::string log;

        auto append = [&](const char* msg, int delay_ms) -> task<> {
            co_await m.lock();
            co_await sleep(delay_ms, loop);
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
        std::println("  7a mutex log = \"{}\" (length 3, order depends on lock acquisition)", log);
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

        std::println("  7b event: consumer_saw_it = {}", consumer_saw_it ? "true" : "false");
    }

    std::println("");
}

// ============================================================
// 8. Combining patterns — scope + when_all + cancellation
// ============================================================

void example_combined() {
    std::println("--- 8. Combined patterns ---");

    event_loop loop;

    // Use a scope to spawn workers, each doing a when_all internally,
    // with external cancellation cutting everything short.

    cancellation_source source;
    int completed_pairs = 0;

    auto pair_work = [&](int id) -> task<> {
        auto a = [&]() -> task<int> {
            co_await sleep(5 + id, loop);
            co_return id * 10;
        };
        auto b = [&]() -> task<int> {
            co_await sleep(5 + id, loop);
            co_return id * 20;
        };
        auto [x, y] = co_await when_all(a(), b());
        completed_pairs += 1;
        std::println("  pair {}: {} + {} = {}", id, x, y, x + y);
    };

    auto driver = [&]() -> task<int> {
        async_scope scope;
        for(int i = 0; i < 5; ++i) {
            scope.spawn(pair_work(i));
        }
        co_await scope;
        co_return completed_pairs;
    };

    auto guarded = with_token(driver(), source.token());

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
        std::println("  all done: {} pairs completed", *result);
    } else {
        std::println("  cancelled after {} pairs", completed_pairs);
    }
}

// ============================================================

int main() {
    std::println("=== kotatsu async examples ===");
    std::println("");

    example_basic_tasks();
    example_timers();
    example_when_all();
    example_when_any();
    example_async_scope();
    example_cancellation();
    example_sync_primitives();
    example_combined();

    std::println("");
    std::println("=== done ===");
    return 0;
}
