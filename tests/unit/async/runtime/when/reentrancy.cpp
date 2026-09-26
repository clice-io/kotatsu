// ZEST_SUITE(when_reentrancy): reentrant / arm-phase / looping cancel scenarios
// for when_all/when_any. Covers deferred sync resumes where the signaler wins,
// reentrant cancellation-token firing, looping tasks that must stop at the next
// checkpoint when cancelled mid-loop, and the first_cancel_child bookkeeping
// repros (sync child completing during arm + external cancel). Regular cancel
// semantics live in cancel.cpp.
#include "../../loop_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

ZEST_SUITE(when_reentrancy){

    // b resumes from sleep, calls ev.set() (deferred resume for a), then co_returns.
    // b completes first — with deferred resume, the signaler finishes before the
    // waiter runs.
    ZEST_CASE(any_deferred_sync_resume_signaler_wins){event ev;

auto a = [&]() -> task<int> {
    co_await ev.wait();
    co_return 1;
};

auto b = [&]() -> task<int> {
    co_await sleep(1);
    ev.set();
    co_return 2;
};

auto combined = [&]() -> task<std::variant<int, int>> {
    co_return co_await when_any(a(), b());
};

auto [winner] = run(combined());
EXPECT(winner.has_value());
EXPECT(winner->index() == 1U);
EXPECT(std::get<1>(*winner) == 2);

}  // namespace kota

// Semaphore variant: b releases, then completes before a resumes.
ZEST_CASE(any_deferred_semaphore_release) {
    semaphore sem;

    auto a = [&]() -> task<int> {
        co_await sem.acquire();
        co_return 1;
    };

    auto b = [&]() -> task<int> {
        co_await sleep(1);
        sem.release();
        co_return 2;
    };

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(a(), b());
    };

    auto [winner] = run(combined());
    EXPECT(winner.has_value());
    EXPECT(winner->index() == 1U);
    EXPECT(std::get<1>(*winner) == 2);
}

// Mutex variant: a unlocks (defers b's resume), then co_returns.
// a completes first since b's resume is deferred.
ZEST_CASE(any_deferred_mutex_unlock) {
    mutex m;

    auto a = [&]() -> task<int> {
        co_await m.lock();
        co_await sleep(1);
        m.unlock();
        co_return 1;
    };

    auto b = [&]() -> task<int> {
        co_await m.lock();
        co_return 2;
    };

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(a(), b());
    };

    auto [winner] = run(combined());
    EXPECT(winner.has_value());
    EXPECT(winner->index() == 0U);
    EXPECT(std::get<0>(*winner) == 1);
}

// CV variant: b notifies (defers a's resume), then co_returns. b wins.
ZEST_CASE(any_deferred_cv_notify) {
    mutex m;
    condition_variable cv;

    auto a = [&]() -> task<int> {
        co_await m.lock();
        co_await cv.wait(m);
        m.unlock();
        co_return 1;
    };

    auto b = [&]() -> task<int> {
        co_await sleep(1);
        cv.notify_one();
        co_return 2;
    };

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(a(), b());
    };

    auto [winner] = run(combined());
    EXPECT(winner.has_value());
    EXPECT(winner->index() == 1U);
    EXPECT(std::get<1>(*winner) == 2);
}

// cancellation_token variant: src.cancel() fires an internal event that
// resumes the token's wait task inline, triggering reentrancy.
ZEST_CASE(any_reentrant_cancellation_token) {
    cancellation_source src;

    auto slow = [&]() -> task<int> {
        co_await sleep(1000);
        co_return 1;
    };

    auto canceler = [&]() -> task<int> {
        co_await sleep(1);
        src.cancel();
        co_return 2;
    };

    auto guarded = with_token(slow(), src.token());
    auto cancel_task = canceler();
    run(guarded, cancel_task);

    EXPECT(!guarded.value().has_value());
}

// when_all variant: reentrancy during when_all should not cause issues either.
ZEST_CASE(all_reentrant_event_set) {
    event ev;
    int a_done = 0;
    int b_done = 0;

    auto a = [&]() -> task<> {
        co_await ev.wait();
        a_done = 1;
    };

    auto b = [&]() -> task<> {
        co_await sleep(1);
        ev.set();
        b_done = 1;
    };

    auto combined = [&]() -> task<> {
        co_await when_all(a(), b());
    };

    run(combined());
    EXPECT(a_done == 1);
    EXPECT(b_done == 1);
}

// Multiple waiters: c sets event (defers a, b), then co_returns. c wins.
ZEST_CASE(any_deferred_event_multiple_waiters) {
    event ev;

    auto a = [&]() -> task<int> {
        co_await ev.wait();
        co_return 1;
    };

    auto b = [&]() -> task<int> {
        co_await ev.wait();
        co_return 2;
    };

    auto c = [&]() -> task<int> {
        co_await sleep(1);
        ev.set();
        co_return 3;
    };

    auto combined = [&]() -> task<std::variant<int, int, int>> {
        co_return co_await when_any(a(), b(), c());
    };

    auto [winner] = run(combined());
    EXPECT(winner.has_value());
    EXPECT(winner->index() == 2U);
    EXPECT(std::get<2>(*winner) == 3);
}

// Repro: a looping task that gets cancelled reentrantly should stop at the
// next co_await, not loop forever.  b loops calling ev.set() each iteration;
// a waits on ev and completes, triggering when_any cancel on b mid-loop.
// If the Cancelled state is lost, b runs all 100 iterations.
ZEST_CASE(any_reentrant_cancel_stops_looping_task) {
    event ev;
    int loop_count = 0;

    auto a = [&]() -> task<int> {
        co_await ev.wait();
        co_return 1;
    };

    auto b = [&]() -> task<int> {
        for(int i = 0; i < 100; ++i) {
            co_await sleep(1);
            loop_count = i + 1;
            ev.set();
        }
        co_return 2;
    };

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(a(), b());
    };

    auto [winner] = run(combined());
    EXPECT(winner.has_value());
    EXPECT(winner->index() == 0U);
    EXPECT(std::get<0>(*winner) == 1);
    EXPECT(loop_count < 10);
}

// Semaphore variant: b loops releasing a semaphore each iteration.
// a acquires and completes, when_any cancels b mid-loop.
ZEST_CASE(any_reentrant_cancel_stops_looping_task_semaphore) {
    semaphore sem;
    int loop_count = 0;

    auto a = [&]() -> task<int> {
        co_await sem.acquire();
        co_return 1;
    };

    auto b = [&]() -> task<int> {
        for(int i = 0; i < 100; ++i) {
            co_await sleep(1);
            loop_count = i + 1;
            sem.release();
        }
        co_return 2;
    };

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(a(), b());
    };

    auto [winner] = run(combined());
    EXPECT(winner.has_value());
    EXPECT(winner->index() == 0U);
    EXPECT(std::get<0>(*winner) == 1);
    EXPECT(loop_count < 10);
}

// when_all variant: b loops and triggers a's completion via event.
// Both should complete; b's cancel should terminate its loop.
ZEST_CASE(all_reentrant_cancel_stops_looping_task_on_error) {
    event ev;
    int loop_count = 0;

    auto a = [&]() -> task<int, error> {
        co_await ev.wait();
        co_await fail(error::connection_refused);
    };

    auto b = [&]() -> task<int, error> {
        for(int i = 0; i < 100; ++i) {
            co_await sleep(1);
            loop_count = i + 1;
            ev.set();
        }
        co_return 2;
    };

    auto combined = [&]() -> task<std::tuple<int, int>, error> {
        co_return co_await when_all(a(), b());
    };

    auto [result] = run(combined());
    EXPECT(result.has_value());
    EXPECT(result->has_error());
    EXPECT(loop_count < 10);
}

// Multiple co_await points after the cancel trigger: the cancelled task does
// several co_awaits in a row before looping, all should finalize promptly.
ZEST_CASE(any_reentrant_cancel_stops_after_multiple_awaits) {
    event ev;
    int step = 0;

    auto a = [&]() -> task<int> {
        co_await ev.wait();
        co_return 1;
    };

    auto b = [&]() -> task<int> {
        co_await sleep(1);
        ev.set();
        step = 1;
        co_await sleep(1);
        step = 2;
        co_await sleep(1);
        step = 3;
        co_return 2;
    };

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(a(), b());
    };

    auto [winner] = run(combined());
    EXPECT(winner.has_value());
    EXPECT(winner->index() == 0U);
    EXPECT(step == 1);
}

// Condition variable loop: b repeatedly notifies cv; a waits and completes.
ZEST_CASE(any_reentrant_cancel_stops_looping_task_cv) {
    mutex m;
    condition_variable cv;
    int loop_count = 0;

    auto a = [&]() -> task<int> {
        co_await m.lock();
        co_await cv.wait(m);
        m.unlock();
        co_return 1;
    };

    auto b = [&]() -> task<int> {
        for(int i = 0; i < 100; ++i) {
            co_await sleep(1);
            loop_count = i + 1;
            cv.notify_one();
        }
        co_return 2;
    };

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(a(), b());
    };

    auto [winner] = run(combined());
    EXPECT(winner.has_value());
    EXPECT(winner->index() == 0U);
    EXPECT(std::get<0>(*winner) == 1);
    EXPECT(loop_count < 10);
}

// Repro: when_all with a synchronously-completing child, two with_token
// children waiting on the same event, and an external cancel arriving after
// the event fires.  The sync child completes during the arm phase, and the
// external cancel arrives while the inner when_any (inside with_token) has
// already latched a Resume from its winning child.  async_node::cancel()
// overwrites deferred to Cancel via defer_cancel(), but first_cancel_child
// was never recorded — await_resume would index out of bounds.
ZEST_CASE(all_sync_child_with_token_external_cancel) {
    event ev;
    cancellation_source source;

    auto request = [&]() -> task<int> {
        co_await ev.wait();
        co_return 42;
    };

    auto driver = []() -> task<> {
        co_return;
    };

    auto combined = [&]() -> task<> {
        auto result = co_await when_all{
            with_token(request(), source.token()),
            with_token(request(), source.token()),
            driver(),
        };

        (void)result;
    };

    auto trigger = [&]() -> task<> {
        co_await sleep(1);
        ev.set();
        co_await sleep(1);
        source.cancel();
    };

    auto main_task = combined();
    auto trigger_task = trigger();
    run(main_task, trigger_task);
    EXPECT(main_task->is_finished());
}

// Same scenario as above but the token fires BEFORE the event, so
// with_token's inner when_any is cancelled while still waiting for
// the request.  This exercises first_cancel_child being set from the
// cancelled token.wait() child even when deferred was already None.
ZEST_CASE(all_sync_child_with_token_cancel_before_event) {
    event ev;
    cancellation_source source;
    bool got_cancel = false;

    auto request = [&]() -> task<int> {
        co_await ev.wait();
        co_return 42;
    };

    auto driver = []() -> task<> {
        co_return;
    };

    auto combined = [&]() -> task<> {
        auto result = co_await when_all{
            with_token(request(), source.token()),
            with_token(request(), source.token()),
            driver(),
        };

        got_cancel = result.is_cancelled();
    };

    auto trigger = [&]() -> task<> {
        co_await sleep(1);
        source.cancel();
    };

    auto main_task = combined();
    auto trigger_task = trigger();
    run(main_task, trigger_task);
    EXPECT(main_task->is_finished());
    EXPECT(got_cancel);
}

// Minimal trigger for the first_cancel_child bookkeeping bug:
// The driver fires the cancellation token AND the shared event during the
// arm phase (source.cancel() first, then ev.set()).  Because the token's
// internal event.set() is called before the shared event's set(), the
// token-side deferred resumes appear before the event-side resumes in the
// deferred queue.  Processing the token resumes first makes the inner
// when_any (inside with_token) latch Deferred::Cancel.  Then when the
// event-side resumes arrive, the second cancelled child must still update
// first_cancel_child — otherwise tuple_visit_at_return hits index npos.
ZEST_CASE(all_sync_driver_fires_cancel_then_event) {
    event ev;
    cancellation_source source;
    bool got_cancel = false;

    auto request = [&]() -> task<int> {
        co_await ev.wait();
        co_return 42;
    };

    auto driver = [&]() -> task<> {
        source.cancel();
        ev.set();
        co_return;
    };

    auto combined = [&]() -> task<> {
        auto result = co_await when_all{
            with_token(request(), source.token()),
            with_token(request(), source.token()),
            driver(),
        };

        got_cancel = result.is_cancelled();
    };

    auto main_task = combined();
    run(main_task);
    EXPECT(main_task->is_finished());
    EXPECT(got_cancel);
}

// Core repro for the first_cancel_child corruption bug.
//
// Trigger chain:
//   1. Event fires → request_A completes and calls source.cancel()
//   2. Inner when_any_A: winner = 0, deferred = Resume, cancel_siblings
//   3. cancel_siblings cancels token_wait_A; the EventWaiter on the token's
//      event was already Finished (from source.cancel()'s event.set()), so
//      cancel is a no-op.  But resume_and_drain drains the token-side deferred
//      resumes, which includes token_event_wait_B.
//   4. token_event_wait_B → token_wait_B → Cancelled → inner when_any_B:
//      Cancel → with_token_B Cancelled → outer when_all cancel_siblings →
//      cancel with_token_A → inner when_any_A cancel() → defer_cancel()
//      overwrites Resume → first_cancel_child was never set → CRASH.
//
// The sync driver completing during arm is essential: it means the outer
// when_all has completed == 1 before the event fires, so after with_token_B
// cancels, the outer when_all can cancel with_token_A.
ZEST_CASE(sync_driver_event_fires_cancel_cascade) {
    event ev;
    event pending;
    cancellation_source source;
    bool got_cancel = false;

    auto request_a = [&]() -> task<int> {
        co_await ev.wait();
        source.cancel();
        co_return 42;
    };

    auto request_b = [&]() -> task<int> {
        co_await pending.wait();
        co_return 99;
    };

    auto driver = []() -> task<> {
        co_return;
    };

    auto combined = [&]() -> task<> {
        auto result = co_await when_all{
            with_token(request_b(), source.token()),
            with_token(request_a(), source.token()),
            driver(),
        };
        got_cancel = result.is_cancelled();
    };

    auto trigger = [&]() -> task<> {
        co_await sleep(1);
        ev.set();
    };

    auto main_task = combined();
    auto trigger_task = trigger();
    run(main_task, trigger_task);
    EXPECT(main_task->is_finished());
    EXPECT(got_cancel);
}
}
;  // ZEST_SUITE(when_reentrancy)

}  // namespace kota
