#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

#include "async/harness/io.h"
#include "async/harness/loop_fixture.h"
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

ZEST_SUITE(async_io_watcher, test::LoopFixture) {

ZEST_CASE(timers_fire_in_timeout_order) {
    auto slow = timer::create(loop);
    auto fast = timer::create(loop);
    ASSERT(!slow.start(20ms));
    ASSERT(!fast.start(1ms));
    std::vector<int> order;
    auto waiter = [&](timer& t, int id) -> task<void, error> {
        co_await t.wait().or_fail();
        order.push_back(id);
    };

    auto [slow_waited, fast_waited] = run(waiter(slow, 2), waiter(fast, 1));
    EXPECT(slow_waited.has_value());
    EXPECT(fast_waited.has_value());
    EXPECT(order == std::vector{1, 2});
}

ZEST_CASE(repeating_timer_fires_until_stopped) {
    auto t = timer::create(loop);
    ASSERT(!t.start(1ms, 1ms));
    auto waiter = [&]() -> task<std::size_t, error> {
        co_await wait_three_times(t).or_fail();
        EXPECT(!t.stop());
        // A fire can land between the third wake and the stop, and the timer
        // keeps it for the next wait; take it first.
        co_await winner(t.wait(), yield()).or_fail();
        co_return co_await winner(t.wait(), sleep(20ms)).or_fail();
    };

    auto [result] = run(waiter());
    ASSERT(result.has_value());
    // The stopped timer fired no more: it lost the race to the sleep.
    EXPECT(*result == 1U);
}

ZEST_CASE(timer_keeps_a_fire_nobody_waited_for) {
    auto t = timer::create(loop);
    ASSERT(!t.start(1ms));
    auto waiter = [&]() -> task<std::size_t, error> {
        co_await sleep(20ms);
        co_return co_await winner(t.wait(), sleep(1s)).or_fail();
    };

    auto [result] = run(waiter());
    ASSERT(result.has_value());
    EXPECT(*result == 0U);
}

// The fire kept from before a restart is dropped: the next wait waits for the
// new timeout, and the yield wins.
ZEST_CASE(restarted_timer_drops_a_kept_fire) {
    auto t = timer::create(loop);
    ASSERT(!t.start(1ms));
    auto waiter = [&]() -> task<std::size_t, error> {
        co_await sleep(20ms);
        EXPECT(!t.start(1h));
        co_return co_await winner(t.wait(), yield()).or_fail();
    };

    auto [result] = run(waiter());
    ASSERT(result.has_value());
    EXPECT(*result == 1U);
}

// Some twenty fires go by while the sleep runs; the timer keeps one of them.
ZEST_CASE(timer_keeps_one_of_the_fires_nobody_waited_for) {
    auto t = timer::create(loop);
    ASSERT(!t.start(1ms, 1ms));
    auto waiter = [&]() -> task<std::size_t, error> {
        co_await sleep(20ms);
        EXPECT(!t.stop());
        co_await t.wait().or_fail();
        co_return co_await winner(t.wait(), yield()).or_fail();
    };

    auto [result] = run(waiter());
    ASSERT(result.has_value());
    // No second fire was kept: the yield won.
    EXPECT(*result == 1U);
}

// The timer is far from firing, so only the cancel can end the first wait.
ZEST_CASE(timer_wait_can_be_cancelled) {
    auto t = timer::create(loop);
    ASSERT(!t.start(1h));

    auto [result] = run(winner(t.wait(), yield(loop)));
    ASSERT(result.has_value());
    EXPECT(*result == 1U);
}

// A fire can come before the yield on a slow machine; the waits race again
// until one is cancelled.
ZEST_CASE(cancelled_wait_leaves_the_timer_running) {
    auto t = timer::create(loop);
    ASSERT(!t.start(50ms, 50ms));
    auto waiter = [&]() -> task<void, error> {
        while(co_await winner(t.wait(), yield()).or_fail() == 0) {}
        // The timer runs on: the next wait gets its fire.
        co_await t.wait().or_fail();
    };

    auto [result] = run(waiter());
    EXPECT(result.has_value());
}

ZEST_CASE(sleep_resumes_after_its_timeout) {
    auto sleeper = []() -> task<> {
        co_await sleep(1);
        co_await sleep(1ms);
    };

    auto [result] = run(sleeper());
    EXPECT(result.has_value());
}

ZEST_CASE(shorter_sleep_wins_a_race) {
    auto race = []() -> task<std::size_t> {
        auto winner = co_await when_any(sleep(20ms), sleep(1ms));
        co_return winner.index();
    };

    auto [result] = run(race());
    ASSERT(result.has_value());
    EXPECT(*result == 1U);
}

ZEST_CASE(sleep_can_be_cancelled) {
    auto [result] = run(winner(sleep(1h, loop), yield(loop)));
    ASSERT(result.has_value());
    EXPECT(*result == 1U);
}

// prepare and check wake around the poll, which blocks when nothing else is
// due; the running idle watcher keeps the iterations coming.
ZEST_CASE(tick_watchers_wake_every_iteration) {
    auto on_idle = idle::create(loop);
    auto on_prepare = prepare::create(loop);
    auto on_check = check::create(loop);
    ASSERT(!on_idle.start());
    ASSERT(!on_prepare.start());
    ASSERT(!on_check.start());

    auto [idled, prepared, checked] =
        run(wait_three_times(on_idle), wait_three_times(on_prepare), wait_three_times(on_check));
    ASSERT(idled.has_value());
    EXPECT(*idled == 3);
    ASSERT(prepared.has_value());
    EXPECT(*prepared == 3);
    ASSERT(checked.has_value());
    EXPECT(*checked == 3);
}

// Three yields let the started watchers fire on three iterations nobody
// waits in; stopped, they keep one of those fires.
ZEST_CASE(tick_watchers_keep_one_of_the_fires_nobody_waited_for) {
    auto on_idle = idle::create(loop);
    auto on_prepare = prepare::create(loop);
    auto on_check = check::create(loop);
    auto kept = [&](auto& watcher) -> task<std::size_t, error> {
        EXPECT(!watcher.start());
        for(int i = 0; i < 3; ++i) {
            co_await yield();
        }
        EXPECT(!watcher.stop());
        co_await watcher.wait().or_fail();
        co_return co_await winner(watcher.wait(), yield()).or_fail();
    };

    auto [idled, prepared, checked] = run(kept(on_idle), kept(on_prepare), kept(on_check));
    ASSERT(idled.has_value());
    EXPECT(*idled == 1U);
    ASSERT(prepared.has_value());
    EXPECT(*prepared == 1U);
    ASSERT(checked.has_value());
    EXPECT(*checked == 1U);
}

// Each wait is cancelled as soon as it starts; the watchers run on, and the
// next waits get their fires.
ZEST_CASE(cancelled_waits_leave_the_tick_watchers_running) {
    auto on_idle = idle::create(loop);
    auto on_prepare = prepare::create(loop);
    auto on_check = check::create(loop);
    auto waiter = [&](auto& watcher) -> task<std::size_t, error> {
        EXPECT(!watcher.start());
        auto first = co_await winner(watcher.wait(), finished()).or_fail();
        co_await watcher.wait().or_fail();
        co_return first;
    };

    auto [idled, prepared, checked] = run(waiter(on_idle), waiter(on_prepare), waiter(on_check));
    ASSERT(idled.has_value());
    EXPECT(*idled == 1U);
    ASSERT(prepared.has_value());
    EXPECT(*prepared == 1U);
    ASSERT(checked.has_value());
    EXPECT(*checked == 1U);
}

// The watchers are not started, so only the cancel can end the waits.
ZEST_CASE(tick_watcher_waits_can_be_cancelled) {
    auto on_idle = idle::create(loop);
    auto on_prepare = prepare::create(loop);
    auto on_check = check::create(loop);

    auto [idled, prepared, checked] = run(winner(on_idle.wait(), yield(loop)),
                                          winner(on_prepare.wait(), yield(loop)),
                                          winner(on_check.wait(), yield(loop)));
    ASSERT(idled.has_value());
    EXPECT(*idled == 1U);
    ASSERT(prepared.has_value());
    EXPECT(*prepared == 1U);
    ASSERT(checked.has_value());
    EXPECT(*checked == 1U);
}

ZEST_CASE(second_wait_while_one_is_pending_fails) {
    auto t = timer::create(loop);
    auto on_idle = idle::create(loop);
    auto on_prepare = prepare::create(loop);
    auto on_check = check::create(loop);

    auto [timed, idled, prepared, checked] =
        run(second_wait(t), second_wait(on_idle), second_wait(on_prepare), second_wait(on_check));
    ASSERT(timed.has_value());
    EXPECT(*timed == error::resource_busy_or_locked);
    ASSERT(idled.has_value());
    EXPECT(*idled == error::resource_busy_or_locked);
    ASSERT(prepared.has_value());
    EXPECT(*prepared == error::resource_busy_or_locked);
    ASSERT(checked.has_value());
    EXPECT(*checked == error::resource_busy_or_locked);
}

// stop() is not sticky: after a start(), the next wait gets the next fire.
ZEST_CASE(stop_ends_a_pending_wait) {
    auto t = timer::create(loop);
    ASSERT(!t.start(1h));
    auto stop_it = [&]() -> task<error> {
        co_return t.stop();
    };
    auto restarted = [&]() -> task<void, error> {
        EXPECT(!t.start(1ms));
        co_await t.wait().or_fail();
    };

    auto [waited, stopped] = run(t.wait(), stop_it());
    ASSERT(waited.has_error());
    EXPECT(waited.error() == error::operation_aborted);
    ASSERT(stopped.has_value());
    EXPECT(!*stopped);
    auto [again] = run(restarted());
    EXPECT(again.has_value());
}

ZEST_CASE(wait_ended_by_destroying_its_watcher_fails) {
    std::optional<timer> t = timer::create(loop);
    auto destroy = [&]() -> task<> {
        t.reset();
        co_return;
    };

    auto [waited, destroyed] = run(t->wait(), destroy());
    ASSERT(waited.has_error());
    EXPECT(waited.error() == error::operation_aborted);
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
    ASSERT(result.has_value());
    EXPECT(*result == 1U);
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
    ASSERT(result.has_value());
    EXPECT(*result == std::vector<error>(5, error::invalid_argument));
    EXPECT(inert_timer.start(1ms) == error::invalid_argument);
    EXPECT(inert_idle.start() == error::invalid_argument);
    EXPECT(inert_prepare.start() == error::invalid_argument);
    EXPECT(inert_check.start() == error::invalid_argument);
    EXPECT(inert_signal.start(1) == error::invalid_argument);
    EXPECT(inert_timer.stop() == error::invalid_argument);
    EXPECT(inert_signal.stop() == error::invalid_argument);
}

};  // ZEST_SUITE(async_io_watcher)

}  // namespace

}  // namespace kota
