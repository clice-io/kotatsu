#include <chrono>
#include <cstddef>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

using namespace std::literals;

template <typename Watcher>
task<int> wait_three_times(Watcher& watcher) {
    for(int i = 0; i < 3; ++i) {
        co_await watcher.wait();
    }
    co_return 3;
}

/// Counts every wake of `watcher` in `ticks`, setting `ticked` at the first.
template <typename Watcher>
task<> count_ticks(Watcher& watcher, int& ticks, event& ticked) {
    while(true) {
        co_await watcher.wait();
        ticks += 1;
        ticked.set();
    }
}

ZEST_SUITE(async_io_watcher, test::LoopFixture) {

ZEST_CASE(timers_fire_in_timeout_order) {
    auto slow = timer::create(loop);
    auto fast = timer::create(loop);
    slow.start(20ms);
    fast.start(1ms);
    std::vector<int> order;
    auto waiter = [&](timer& t, int id) -> task<> {
        co_await t.wait();
        order.push_back(id);
    };

    auto [slow_waited, fast_waited] = run(waiter(slow, 2), waiter(fast, 1));
    EXPECT(slow_waited.has_value());
    EXPECT(fast_waited.has_value());
    EXPECT(order == std::vector{1, 2});
}

ZEST_CASE(repeating_timer_fires_until_stopped) {
    auto t = timer::create(loop);
    t.start(1ms, 1ms);
    auto waiter = [&]() -> task<std::size_t> {
        co_await wait_three_times(t);
        t.stop();
        auto next = co_await when_any(t.wait(), sleep(20ms));
        co_return next.index();
    };

    auto [result] = run(waiter());
    ASSERT(result.has_value());
    // The stopped timer lost the race to the sleep.
    EXPECT(*result == 1U);
}

ZEST_CASE(timer_keeps_a_fire_nobody_waited_for) {
    auto t = timer::create(loop);
    t.start(1ms);
    auto waiter = [&]() -> task<std::size_t> {
        co_await sleep(20ms);
        auto first = co_await when_any(t.wait(), sleep(1s));
        co_return first.index();
    };

    auto [result] = run(waiter());
    ASSERT(result.has_value());
    EXPECT(*result == 0U);
}

ZEST_CASE(timer_wait_can_be_cancelled) {
    auto t = timer::create(loop);
    t.start(1h);
    auto waiter = [&]() -> task<> {
        co_await t.wait();
    };
    auto target = waiter();
    auto* node = target.operator->();
    auto cancel_it = [&]() -> task<> {
        node->cancel();
        co_return;
    };

    auto [waited, driver] = run(std::move(target), cancel_it());
    EXPECT(waited.is_cancelled());
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
    auto sleeper = []() -> task<> {
        co_await sleep(1h);
    };
    auto target = sleeper();
    auto* node = target.operator->();
    auto cancel_it = [&]() -> task<> {
        node->cancel();
        co_return;
    };

    auto [slept, driver] = run(std::move(target), cancel_it());
    EXPECT(slept.is_cancelled());
}

// prepare and check wake around the poll, which blocks when nothing else is
// due; the running idle watcher keeps the iterations coming.
ZEST_CASE(tick_watchers_wake_every_iteration) {
    auto on_idle = idle::create(loop);
    auto on_prepare = prepare::create(loop);
    auto on_check = check::create(loop);
    on_idle.start();
    on_prepare.start();
    on_check.start();

    auto [idled, prepared, checked] =
        run(wait_three_times(on_idle), wait_three_times(on_prepare), wait_three_times(on_check));
    ASSERT(idled.has_value());
    EXPECT(*idled == 3);
    ASSERT(prepared.has_value());
    EXPECT(*prepared == 3);
    ASSERT(checked.has_value());
    EXPECT(*checked == 3);
}

// Once every watcher has woken its waiter, the canceller stops the watchers,
// so that only the cancel itself can end the waits pending on them.
ZEST_CASE(tick_watcher_waits_can_be_cancelled) {
    auto on_idle = idle::create(loop);
    auto on_prepare = prepare::create(loop);
    auto on_check = check::create(loop);
    on_idle.start();
    on_prepare.start();
    on_check.start();
    int ticks[3] = {};
    event ticked[3];
    auto idling = count_ticks(on_idle, ticks[0], ticked[0]);
    auto preparing = count_ticks(on_prepare, ticks[1], ticked[1]);
    auto checking = count_ticks(on_check, ticks[2], ticked[2]);
    async_node* nodes[] = {idling.operator->(), preparing.operator->(), checking.operator->()};
    auto cancel_all = [&]() -> task<> {
        for(auto& first: ticked) {
            co_await first.wait();
        }
        on_idle.stop();
        on_prepare.stop();
        on_check.stop();
        for(auto* node: nodes) {
            node->cancel();
        }
    };

    auto [idled, prepared, checked, driver] =
        run(std::move(idling), std::move(preparing), std::move(checking), cancel_all());
    EXPECT(idled.is_cancelled());
    EXPECT(prepared.is_cancelled());
    EXPECT(checked.is_cancelled());
    EXPECT(ticks[0] > 0);
    EXPECT(ticks[1] > 0);
    EXPECT(ticks[2] > 0);
}

// A default-constructed watcher watches nothing: waits end at once, and a
// signal reports that it has no handle.
ZEST_CASE(inert_watchers_do_nothing) {
    timer inert_timer;
    idle inert_idle;
    signal inert_signal;
    inert_timer.start(1ms);
    inert_timer.stop();
    auto waits = [&]() -> task<result<void>> {
        co_await inert_timer.wait();
        co_await inert_idle.wait();
        co_return co_await inert_signal.wait();
    };

    auto [result] = run(waits());
    ASSERT(result.has_value());
    ASSERT(result->has_error());
    EXPECT(result->error() == error::invalid_argument);
    EXPECT(inert_signal.start(1) == error::invalid_argument);
    EXPECT(inert_signal.stop() == error::invalid_argument);
}

};  // ZEST_SUITE(async_io_watcher)

}  // namespace

}  // namespace kota
