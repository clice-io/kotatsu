#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

#include "async/harness/io.h"
#include "async/harness/pending_op.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

using namespace std::literals;

template <typename Watcher>
task<int, error> wait_three_times(Watcher& watcher) {
    for(int i = 0; i < 3; ++i) {
        co_await watcher.wait().or_fail();
    }
    co_return 3;
}

using test::finished;
using test::winner;

/// What a second wait() gets while `watcher` has one pending; the first is
/// withdrawn once the second has failed.
template <typename Watcher>
task<error> second_wait(Watcher& watcher) {
    auto both = co_await when_any(watcher.wait(), watcher.wait());
    co_return both.has_error() ? both.error() : error();
}

ZEST_SUITE(async_io_watcher, zest::LoopFixture) {

ZEST_CASE(timers_fire_in_timeout_order) {
    auto slow = timer::create(loop);
    auto fast = timer::create(loop);
    ZASSERT(!slow.start(20ms));
    ZASSERT(!fast.start(1ms));
    std::vector<int> order;
    auto waiter = [&](timer& t, int id) -> task<void, error> {
        co_await t.wait().or_fail();
        order.push_back(id);
    };

    auto [slow_waited, fast_waited] = run(waiter(slow, 2), waiter(fast, 1));
    ZEXPECT(slow_waited.has_value());
    ZEXPECT(fast_waited.has_value());
    ZEXPECT(order == std::vector{1, 2});
}

ZEST_CASE(repeating_timer_fires_until_stopped) {
    auto t = timer::create(loop);
    ZASSERT(!t.start(1ms, 1ms));
    auto waiter = [&]() -> task<std::size_t, error> {
        co_await wait_three_times(t).or_fail();
        ZEXPECT(!t.stop());
        // A fire can land between the third wake and the stop, and the timer
        // keeps it for the next wait; take it first.
        co_await winner(t.wait(), yield()).or_fail();
        co_return co_await winner(t.wait(), sleep(20ms)).or_fail();
    };

    auto [result] = run(waiter());
    ZASSERT(result.has_value());
    // The stopped timer fired no more: it lost the race to the sleep.
    ZEXPECT(*result == 1U);
}

ZEST_CASE(timer_keeps_a_fire_nobody_waited_for) {
    auto t = timer::create(loop);
    ZASSERT(!t.start(1ms));
    auto waiter = [&]() -> task<std::size_t, error> {
        co_await sleep(20ms);
        co_return co_await winner(t.wait(), sleep(1s)).or_fail();
    };

    auto [result] = run(waiter());
    ZASSERT(result.has_value());
    ZEXPECT(*result == 0U);
}

// The fire kept from before a restart is dropped: the next wait waits for the
// new timeout, and the yield wins.
ZEST_CASE(restarted_timer_drops_a_kept_fire) {
    auto t = timer::create(loop);
    ZASSERT(!t.start(1ms));
    auto waiter = [&]() -> task<std::size_t, error> {
        co_await sleep(20ms);
        ZEXPECT(!t.start(1h));
        co_return co_await winner(t.wait(), yield()).or_fail();
    };

    auto [result] = run(waiter());
    ZASSERT(result.has_value());
    ZEXPECT(*result == 1U);
}

// Some twenty fires go by while the sleep runs; the timer keeps one of them.
ZEST_CASE(timer_keeps_one_of_the_fires_nobody_waited_for) {
    auto t = timer::create(loop);
    ZASSERT(!t.start(1ms, 1ms));
    auto waiter = [&]() -> task<std::size_t, error> {
        co_await sleep(20ms);
        ZEXPECT(!t.stop());
        co_await t.wait().or_fail();
        co_return co_await winner(t.wait(), yield()).or_fail();
    };

    auto [result] = run(waiter());
    ZASSERT(result.has_value());
    // No second fire was kept: the yield won.
    ZEXPECT(*result == 1U);
}

// The timer is far from firing, so only the cancel can end the first wait.
ZEST_CASE(timer_wait_can_be_cancelled) {
    auto t = timer::create(loop);
    ZASSERT(!t.start(1h));

    auto [result] = run(winner(t.wait(), yield(loop)));
    ZASSERT(result.has_value());
    ZEXPECT(*result == 1U);
}

// A fire can come before the yield on a slow machine; the waits race again
// until one is cancelled.
ZEST_CASE(cancelled_wait_leaves_the_timer_running) {
    auto t = timer::create(loop);
    ZASSERT(!t.start(50ms, 50ms));
    auto waiter = [&]() -> task<void, error> {
        while(co_await winner(t.wait(), yield()).or_fail() == 0) {}
        // The timer runs on: the next wait gets its fire.
        co_await t.wait().or_fail();
    };

    auto [result] = run(waiter());
    ZEXPECT(result.has_value());
}

ZEST_CASE(sleep_resumes_after_its_timeout) {
    auto sleeper = []() -> task<> {
        co_await sleep(1);
        co_await sleep(1ms);
    };

    auto [result] = run(sleeper());
    ZEXPECT(result.has_value());
}

ZEST_CASE(shorter_sleep_wins_a_race) {
    auto race = []() -> task<std::size_t> {
        auto winner = co_await when_any(sleep(20ms), sleep(1ms));
        co_return winner.index();
    };

    auto [result] = run(race());
    ZASSERT(result.has_value());
    ZEXPECT(*result == 1U);
}

ZEST_CASE(sleep_can_be_cancelled) {
    auto [result] = run(winner(sleep(1h, loop), yield(loop)));
    ZASSERT(result.has_value());
    ZEXPECT(*result == 1U);
}

ZEST_CASE(with_timeout_passes_the_value_through) {
    auto value = []() -> task<int> {
        co_await yield();
        co_return 7;
    };

    auto [result] = run(with_timeout(value(), 1h, loop));
    ZASSERT(result.has_value());
    ZEXPECT(*result == 7);
}

ZEST_CASE(with_timeout_passes_the_error_through) {
    auto failing = []() -> task<int, error> {
        co_await fail(error::connection_refused);
    };

    auto [result] = run(with_timeout(failing(), 1h, loop));
    ZASSERT(result.has_error());
    ZEXPECT(result.error() == error::connection_refused);
}

ZEST_CASE(with_timeout_cancels_a_task_past_its_deadline) {
    event gate;
    auto waiting = [&]() -> task<> {
        co_await gate.wait();
    };

    auto [result] = run(with_timeout(waiting(), 1ms, loop));
    ZEXPECT(result.is_cancelled());
    ZEXPECT(!gate.has_waiters());
}

// A deadline already past lets the task start, and cancels it on the loop's
// next turn.
ZEST_CASE(with_timeout_past_its_deadline_cancels_on_the_next_turn) {
    event gate;
    bool started = false;
    auto waiting = [&]() -> task<> {
        started = true;
        co_await gate.wait();
    };

    auto [result] = run(with_timeout(waiting(), -1ms, loop));
    ZEXPECT(result.is_cancelled());
    ZEXPECT(started);
}

// A cancel from outside ends the task and the deadline's timer with it, long
// before the deadline.
ZEST_CASE(with_timeout_ends_when_cancelled_from_outside) {
    event gate;
    auto waiting = [&]() -> task<> {
        co_await gate.wait();
    };
    auto timed = [&]() -> task<> {
        co_await with_timeout(waiting(), 1h, loop);
    };
    auto watched = timed();
    auto canceller = [&]() -> task<> {
        co_await yield();
        watched.cancel();
    };

    auto [result, cancelling] = run(watched, canceller());
    ZEXPECT(result.is_cancelled());
    ZEXPECT(cancelling.has_value());
    ZEXPECT(!gate.has_waiters());
}

// The deadline cancels the task, which ends only once what it awaits has; the
// timeout ends with it.
ZEST_CASE(with_timeout_ends_once_the_cancelled_task_has) {
    test::PendingOp op;
    auto pending = [&]() -> task<> {
        co_await op;
    };
    bool ended = false;
    auto timed = [&]() -> task<bool> {
        auto timed_out = co_await with_timeout(pending(), 1ms, loop);
        ended = true;
        co_return timed_out.is_cancelled();
    };
    auto completer = [&]() -> task<bool> {
        while(!op.cancel_requested()) {
            co_await yield();
        }
        bool ended_before_the_task = ended;
        op.complete();
        co_return ended_before_the_task;
    };

    auto [result, ended_early] = run(timed(), completer());
    ZASSERT(result.has_value());
    ZEXPECT(*result);
    ZASSERT(ended_early.has_value());
    ZEXPECT(!*ended_early);
}

// prepare and check wake around the poll, which blocks when nothing else is
// due; the running idle watcher keeps the iterations coming.
ZEST_CASE(tick_watchers_wake_every_iteration) {
    auto on_idle = idle::create(loop);
    auto on_prepare = prepare::create(loop);
    auto on_check = check::create(loop);
    ZASSERT(!on_idle.start());
    ZASSERT(!on_prepare.start());
    ZASSERT(!on_check.start());

    auto [idled, prepared, checked] =
        run(wait_three_times(on_idle), wait_three_times(on_prepare), wait_three_times(on_check));
    ZASSERT(idled.has_value());
    ZEXPECT(*idled == 3);
    ZASSERT(prepared.has_value());
    ZEXPECT(*prepared == 3);
    ZASSERT(checked.has_value());
    ZEXPECT(*checked == 3);
}

// Three yields let the started watchers fire on three iterations nobody
// waits in; stopped, they keep one of those fires.
ZEST_CASE(tick_watchers_keep_one_of_the_fires_nobody_waited_for) {
    auto on_idle = idle::create(loop);
    auto on_prepare = prepare::create(loop);
    auto on_check = check::create(loop);
    auto kept = [&](auto& watcher) -> task<std::size_t, error> {
        ZEXPECT(!watcher.start());
        for(int i = 0; i < 3; ++i) {
            co_await yield();
        }
        ZEXPECT(!watcher.stop());
        co_await watcher.wait().or_fail();
        co_return co_await winner(watcher.wait(), yield()).or_fail();
    };

    auto [idled, prepared, checked] = run(kept(on_idle), kept(on_prepare), kept(on_check));
    ZASSERT(idled.has_value());
    ZEXPECT(*idled == 1U);
    ZASSERT(prepared.has_value());
    ZEXPECT(*prepared == 1U);
    ZASSERT(checked.has_value());
    ZEXPECT(*checked == 1U);
}

// Each wait is cancelled as soon as it starts; the watchers run on, and the
// next waits get their fires.
ZEST_CASE(cancelled_waits_leave_the_tick_watchers_running) {
    auto on_idle = idle::create(loop);
    auto on_prepare = prepare::create(loop);
    auto on_check = check::create(loop);
    auto waiter = [&](auto& watcher) -> task<std::size_t, error> {
        ZEXPECT(!watcher.start());
        auto first = co_await winner(watcher.wait(), finished()).or_fail();
        co_await watcher.wait().or_fail();
        co_return first;
    };

    auto [idled, prepared, checked] = run(waiter(on_idle), waiter(on_prepare), waiter(on_check));
    ZASSERT(idled.has_value());
    ZEXPECT(*idled == 1U);
    ZASSERT(prepared.has_value());
    ZEXPECT(*prepared == 1U);
    ZASSERT(checked.has_value());
    ZEXPECT(*checked == 1U);
}

// The watchers are not started, so only the cancel can end the waits.
ZEST_CASE(tick_watcher_waits_can_be_cancelled) {
    auto on_idle = idle::create(loop);
    auto on_prepare = prepare::create(loop);
    auto on_check = check::create(loop);

    auto [idled, prepared, checked] = run(winner(on_idle.wait(), yield(loop)),
                                          winner(on_prepare.wait(), yield(loop)),
                                          winner(on_check.wait(), yield(loop)));
    ZASSERT(idled.has_value());
    ZEXPECT(*idled == 1U);
    ZASSERT(prepared.has_value());
    ZEXPECT(*prepared == 1U);
    ZASSERT(checked.has_value());
    ZEXPECT(*checked == 1U);
}

ZEST_CASE(second_wait_while_one_is_pending_fails) {
    auto t = timer::create(loop);
    auto on_idle = idle::create(loop);
    auto on_prepare = prepare::create(loop);
    auto on_check = check::create(loop);

    auto [timed, idled, prepared, checked] =
        run(second_wait(t), second_wait(on_idle), second_wait(on_prepare), second_wait(on_check));
    ZASSERT(timed.has_value());
    ZEXPECT(*timed == error::resource_busy_or_locked);
    ZASSERT(idled.has_value());
    ZEXPECT(*idled == error::resource_busy_or_locked);
    ZASSERT(prepared.has_value());
    ZEXPECT(*prepared == error::resource_busy_or_locked);
    ZASSERT(checked.has_value());
    ZEXPECT(*checked == error::resource_busy_or_locked);
}

// stop() is not sticky: after a start(), the next wait gets the next fire.
ZEST_CASE(stop_ends_a_pending_wait) {
    auto t = timer::create(loop);
    ZASSERT(!t.start(1h));
    auto stop_it = [&]() -> task<error> {
        co_return t.stop();
    };
    auto restarted = [&]() -> task<void, error> {
        ZEXPECT(!t.start(1ms));
        co_await t.wait().or_fail();
    };

    auto [waited, stopped] = run(t.wait(), stop_it());
    ZASSERT(waited.has_error());
    ZEXPECT(waited.error() == error::operation_aborted);
    ZASSERT(stopped.has_value());
    ZEXPECT(!*stopped);
    auto [again] = run(restarted());
    ZEXPECT(again.has_value());
}

ZEST_CASE(wait_ended_by_destroying_its_watcher_fails) {
    std::optional<timer> t = timer::create(loop);
    auto destroy = [&]() -> task<> {
        t.reset();
        co_return;
    };

    auto [waited, destroyed] = run(t->wait(), destroy());
    ZASSERT(waited.has_error());
    ZEXPECT(waited.error() == error::operation_aborted);
}

// The destroyed timer's wait is cancelled after its destruction has ended
// it, before the loop has resumed it: the cancel leaves that ending alone.
ZEST_CASE(wait_cancelled_after_its_watcher_is_destroyed_ends) {
    std::optional<timer> t = timer::create(loop);
    auto destroy = [&]() -> task<> {
        t.reset();
        co_return;
    };

    auto [result] = run(winner(t->wait(), destroy()));
    ZASSERT(result.has_value());
    ZEXPECT(*result == 1U);
}

// A default-constructed watcher watches nothing.
ZEST_CASE(inert_watcher_fails) {
    timer inert_timer;
    idle inert_idle;
    prepare inert_prepare;
    check inert_check;
    signal inert_signal;
    auto waits = [&]() -> task<std::vector<error>> {
        std::vector<error> errors;
        for(watcher* w: {static_cast<watcher*>(&inert_timer),
                         static_cast<watcher*>(&inert_idle),
                         static_cast<watcher*>(&inert_prepare),
                         static_cast<watcher*>(&inert_check),
                         static_cast<watcher*>(&inert_signal)}) {
            auto waited = co_await w->wait();
            errors.push_back(waited.has_error() ? waited.error() : error());
        }
        co_return errors;
    };

    auto [result] = run(waits());
    ZASSERT(result.has_value());
    ZEXPECT(*result == std::vector<error>(5, error::invalid_argument));
    ZEXPECT(inert_timer.start(1ms) == error::invalid_argument);
    ZEXPECT(inert_idle.start() == error::invalid_argument);
    ZEXPECT(inert_prepare.start() == error::invalid_argument);
    ZEXPECT(inert_check.start() == error::invalid_argument);
    ZEXPECT(inert_signal.start(1) == error::invalid_argument);
    ZEXPECT(inert_timer.stop() == error::invalid_argument);
    ZEXPECT(inert_signal.stop() == error::invalid_argument);
}

};  // ZEST_SUITE(async_io_watcher)

}  // namespace

}  // namespace kota
