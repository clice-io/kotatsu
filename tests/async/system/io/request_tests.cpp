#include <atomic>
#include <semaphore>
#include <thread>
#include <utility>

#include "async/harness/loop_fixture.h"
#include "async/harness/os.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_io_request, test::LoopFixture) {

ZEST_CASE(queue_runs_the_work_on_a_pool_thread) {
    const auto loop_thread = std::this_thread::get_id();
    std::thread::id work_thread;
    auto work = queue([&] { work_thread = std::this_thread::get_id(); }, loop);

    auto [result] = run(std::move(work));
    EXPECT(result.has_value());
    EXPECT(work_thread != std::thread::id());
    EXPECT(work_thread != loop_thread);
}

ZEST_CASE(queue_returns_the_work_value) {
    auto [result] = run(queue([] { return 42; }, loop));
    ASSERT(result.has_value());
    EXPECT(*result == 42);
}

ZEST_CASE(queue_runs_every_work) {
    std::atomic<int> ran = 0;
    auto work = [&] {
        ran.fetch_add(1);
    };

    auto [first, second, third] = run(queue(work, loop), queue(work, loop), queue(work, loop));
    EXPECT(first.has_value());
    EXPECT(second.has_value());
    EXPECT(third.has_value());
    EXPECT(ran.load() == 3);
}

ZEST_CASE(cancel_hook_stays_unused_when_the_work_completes) {
    std::atomic<bool> hook_ran = false;
    auto work = queue([] { return 7; }, function<void()>([&] { hook_ran = true; }), loop);

    auto [result] = run(std::move(work));
    ASSERT(result.has_value());
    EXPECT(*result == 7);
    EXPECT(!hook_ran.load());
}

// With every pool thread busy the work waits in the queue; cancelling it
// there dequeues it, so it never runs and the hook is not called.
ZEST_CASE(cancel_while_queued_drops_the_work) {
    test::BusyPool pool;
    event busy;
    std::atomic<bool> ran = false;
    std::atomic<bool> hook_ran = false;
    auto target = [&]() -> task<void, error> {
        co_await busy.wait();
        co_await queue([&] { ran = true; }, [&] { hook_ran = true; }).or_fail();
    };
    auto work = target();
    auto* node = work.operator->();
    auto cancel_it = [&]() -> task<> {
        co_await busy.wait();
        node->cancel();
        pool.release();
    };

    auto [held, cancelled, driver] = run(pool.hold(busy), std::move(work), cancel_it());
    EXPECT(held.has_value());
    EXPECT(cancelled.is_cancelled());
    EXPECT(!ran.load());
    EXPECT(!hook_ran.load());
}

// Running work cannot be dequeued: the hook, run on the loop thread, is how
// it learns to return early, and the task ends cancelled once it has.
ZEST_CASE(cancel_while_running_calls_the_hook) {
    const auto loop_thread = std::this_thread::get_id();
    event started;
    auto notify = loop.create_relay();
    std::binary_semaphore stop{0};
    std::atomic<bool> hook_on_loop_thread = false;
    auto work = queue(
        [&] {
            notify.send([&] { started.set(); });
            stop.acquire();
            return 1;
        },
        [&] {
            hook_on_loop_thread = std::this_thread::get_id() == loop_thread;
            stop.release();
        },
        loop);
    auto* node = work.operator->();
    auto cancel_it = [&]() -> task<> {
        co_await started.wait();
        node->cancel();
    };

    auto [cancelled, driver] = run(std::move(work), cancel_it());
    EXPECT(cancelled.is_cancelled());
    EXPECT(hook_on_loop_thread.load());
}

};  // ZEST_SUITE(async_io_request)

}  // namespace

}  // namespace kota
