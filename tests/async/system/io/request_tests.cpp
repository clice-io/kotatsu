#include <atomic>
#include <cstddef>
#include <semaphore>
#include <thread>
#include <utility>

#include "async/harness/os.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_io_request, zest::LoopFixture) {

ZEST_CASE(queue_runs_the_work_on_a_pool_thread) {
    const auto loop_thread = std::this_thread::get_id();
    std::thread::id work_thread;
    auto work = queue([&] { work_thread = std::this_thread::get_id(); }, loop);

    auto [result] = run(std::move(work));
    ZEXPECT(result.has_value());
    ZEXPECT(work_thread != std::thread::id());
    ZEXPECT(work_thread != loop_thread);
}

ZEST_CASE(queue_returns_the_work_value) {
    auto [result] = run(queue([] { return 42; }, loop));
    ZASSERT(result.has_value());
    ZEXPECT(*result == 42);
}

ZEST_CASE(queue_runs_every_work) {
    std::atomic<int> ran = 0;
    auto work = [&] {
        ran.fetch_add(1);
    };

    auto [first, second, third] = run(queue(work, loop), queue(work, loop), queue(work, loop));
    ZEXPECT(first.has_value());
    ZEXPECT(second.has_value());
    ZEXPECT(third.has_value());
    ZEXPECT(ran.load() == 3);
}

// The hook runs on the loop thread, if at all.
ZEST_CASE(cancel_hook_stays_unused_when_the_work_completes) {
    bool hook_ran = false;
    auto work = queue([] { return 7; }, [&] { hook_ran = true; }, loop);

    auto [result] = run(std::move(work));
    ZASSERT(result.has_value());
    ZEXPECT(*result == 7);
    ZEXPECT(!hook_ran);
}

// With every pool thread busy the work waits in the queue; cancelling it
// there dequeues it, so it never runs and the hook is not called. Once the
// pool is busy, the work is queued first and the trigger then ends, which
// makes when_any cancel the work.
ZEST_CASE(cancel_while_queued_drops_the_work) {
    test::BusyPool pool;
    event busy;
    std::atomic<bool> ran = false;
    bool hook_ran = false;
    auto target = [&]() -> task<> {
        co_await busy.wait();
        co_await queue([&] { ran = true; }, [&] { hook_ran = true; });
    };
    auto trigger = [&]() -> task<> {
        co_await busy.wait();
    };
    auto cancel_queued = [&]() -> task<std::size_t> {
        auto first = co_await when_any(target(), trigger());
        pool.release();
        co_return first.index();
    };

    auto [held, raced] = run(pool.hold(busy), cancel_queued());
    ZEXPECT(held.has_value());
    ZASSERT(raced.has_value());
    ZEXPECT(*raced == 1U);
    ZEXPECT(!ran.load());
    ZEXPECT(!hook_ran);
}

// Running work cannot be dequeued: the hook, run on the loop thread, is how
// it learns to return early, and the task ends cancelled only once the work
// has returned, which is when when_any returns.
ZEST_CASE(cancel_while_running_calls_the_hook) {
    const auto loop_thread = std::this_thread::get_id();
    event started;
    auto notify = loop.create_relay();
    std::binary_semaphore stop{0};
    std::atomic<bool> returned = false;
    bool hook_on_loop_thread = false;
    auto work = [&] {
        notify.send([&] { started.set(); });
        stop.acquire();
        returned = true;
        return 1;
    };
    auto hook = [&] {
        hook_on_loop_thread = std::this_thread::get_id() == loop_thread;
        stop.release();
    };
    auto cancel_running = [&]() -> task<std::pair<std::size_t, bool>> {
        auto first = co_await when_any(queue(work, hook), started.wait());
        co_return std::pair{first.index(), returned.load()};
    };

    auto [seen] = run(cancel_running());
    ZASSERT(seen.has_value());
    // Cancelled, with the work already returned.
    ZEXPECT(*seen == std::pair<std::size_t, bool>{1, true});
    ZEXPECT(hook_on_loop_thread);
}

};  // ZEST_SUITE(async_io_request)

}  // namespace

}  // namespace kota
