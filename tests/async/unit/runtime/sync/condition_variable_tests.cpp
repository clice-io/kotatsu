#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

/// Awaits `waiter` and resumes even when it is cancelled, so that its frame,
/// and a guard in it, goes as soon as it ends.
task<> owner(task<> waiter) {
    co_await std::move(waiter).catch_cancel();
}

ZEST_SUITE(async_runtime_sync_condition_variable, zest::LoopFixture) {

ZEST_CASE(wait_releases_the_mutex_and_takes_it_back) {
    mutex m;
    condition_variable cv;
    bool ready = false;
    auto waiter = [&]() -> task<bool> {
        co_await m.lock();
        while(!ready) {
            co_await cv.wait(m);
        }
        bool held = !m.try_lock();
        m.unlock();
        co_return held;
    };
    auto notifier = [&]() -> task<> {
        co_await m.lock();
        ready = true;
        cv.notify_one();
        m.unlock();
    };

    auto [held_after_wait, notified] = run(waiter(), notifier());
    ZASSERT(held_after_wait.has_value());
    ZEXPECT(*held_after_wait);
    ZEXPECT(notified.has_value());
    ZEXPECT(m.try_lock());
}

ZEST_CASE(notify_one_wakes_the_first_waiter_only) {
    mutex m;
    condition_variable cv;
    std::vector<int> order;
    auto waiter = [&](int id) -> task<> {
        co_await m.lock();
        co_await cv.wait(m);
        order.push_back(id);
        m.unlock();
    };
    auto notifier = [&]() -> task<std::size_t> {
        cv.notify_one();
        co_await yield();
        auto woken_by_one = order.size();
        cv.notify_one();
        co_return woken_by_one;
    };

    auto [first, second, woken_by_one] = run(waiter(1), waiter(2), notifier());
    ZEXPECT(second.has_value());
    ZASSERT(woken_by_one.has_value());
    ZEXPECT(*woken_by_one == 1U);
    ZEXPECT(order == std::vector{1, 2});
}

ZEST_CASE(notify_all_wakes_every_waiter) {
    mutex m;
    condition_variable cv;
    int woken = 0;
    auto waiter = [&]() -> task<> {
        co_await m.lock();
        co_await cv.wait(m);
        woken += 1;
        m.unlock();
    };
    auto notifier = [&]() -> task<> {
        cv.notify_all();
        co_return;
    };

    auto [first, second, third, driver] = run(waiter(), waiter(), waiter(), notifier());
    ZEXPECT(third.has_value());
    ZEXPECT(woken == 3);
}

ZEST_CASE(notify_without_a_waiter_is_lost) {
    mutex m;
    condition_variable cv;
    cv.notify_one();
    cv.notify_all();
    bool woken = false;
    auto waiter = [&]() -> task<> {
        co_await m.lock();
        co_await cv.wait(m);
        woken = true;
        m.unlock();
    };
    auto notifier = [&]() -> task<bool> {
        co_await yield();
        bool woken_before = woken;
        cv.notify_one();
        co_return woken_before;
    };

    auto [waited, woken_before] = run(waiter(), notifier());
    ZEXPECT(waited.has_value());
    ZASSERT(woken_before.has_value());
    ZEXPECT(!*woken_before);
    ZEXPECT(woken);
}

ZEST_CASE(cancelled_waiter_leaves_the_queue) {
    mutex m;
    condition_variable cv;
    std::vector<int> woken;
    auto waiter = [&](int id) -> task<> {
        co_await m.lock();
        std::lock_guard guard(m, std::adopt_lock);
        co_await cv.wait(m);
        woken.push_back(id);
    };
    auto first = owner(waiter(1));
    auto driver = [&]() -> task<> {
        first.cancel();
        cv.notify_one();
        co_return;
    };

    auto [cancelled, second, drove] = run(first, owner(waiter(2)), driver());
    ZEXPECT(cancelled.is_cancelled());
    ZEXPECT(second.has_value());
    ZEXPECT(woken == std::vector{2});
}

// Like std::condition_variable, a wait ends holding the mutex on every way
// out: one cancelled after it was notified, before it ran, takes the mutex back
// too.
ZEST_CASE(waiter_cancelled_after_notify_ends_holding_the_mutex) {
    mutex m;
    condition_variable cv;
    auto waiter = [&]() -> task<> {
        co_await m.lock();
        co_await cv.wait(m);
        m.unlock();
    };
    auto target = waiter();
    auto driver = [&]() -> task<> {
        cv.notify_one();
        target.cancel();
        co_return;
    };

    auto [cancelled, drove] = run(target, driver());
    ZEXPECT(cancelled.is_cancelled());
    ZEXPECT(!m.try_lock());
}

// The cancel of a wait is delivered only once the wait holds the mutex
// again, so a guard in the waiting frame unlocks it when the frame goes.
ZEST_CASE(cancelled_wait_takes_the_mutex_back_before_it_ends) {
    mutex m;
    condition_variable cv;
    cancellation_source source;
    bool ended = false;
    auto waiter = [&]() -> task<> {
        co_await m.lock();
        std::lock_guard guard(m, std::adopt_lock);
        co_await cv.wait(m);
    };
    auto guarded = [&]() -> task<bool> {
        auto result = co_await with_token(waiter(), source.token());
        ended = true;
        co_return result.is_cancelled();
    };

    struct Seen {
        bool ended_while_held = true;
        bool ended_after_unlock = false;
        bool free_after = false;
    };

    auto driver = [&]() -> task<Seen> {
        Seen seen;
        // The waiter gave the mutex up to wait; take it, then cancel the wait.
        co_await m.lock();
        source.cancel();
        co_await yield();
        seen.ended_while_held = ended;
        // Hands the mutex to the cancelled wait, which ends with it; the guard
        // unlocks it as the waiter's frame goes.
        m.unlock();
        co_await yield();
        seen.ended_after_unlock = ended;
        seen.free_after = m.try_lock();
        co_return seen;
    };

    auto [cancelled, seen] = run(guarded(), driver());
    ZASSERT(cancelled.has_value());
    ZEXPECT(*cancelled);
    ZASSERT(seen.has_value());
    ZEXPECT(!seen->ended_while_held);
    ZEXPECT(seen->ended_after_unlock);
    ZEXPECT(seen->free_after);
}

// A task cancelled before it waits does not wait: it goes on holding the
// mutex it came with.
ZEST_CASE(wait_under_a_cancelled_task_keeps_the_mutex) {
    mutex m;
    condition_variable cv;
    task<> target;
    auto waiter = [&]() -> task<> {
        co_await m.lock();
        target.cancel();
        co_await cv.wait(m);
    };
    target = waiter();

    auto [result] = run(target);
    ZEXPECT(result.is_cancelled());
    ZEXPECT(!cv.has_waiters());
    ZEXPECT(!m.try_lock());
}

// notify_one() hands its notification to the first waiter before that waiter
// runs. A waiter cancelled in between passes it on to the next one, as mutex
// and semaphore pass on what they hand over.
ZEST_CASE(notify_one_to_a_waiter_cancelled_before_it_runs_passes_it_on) {
    mutex m;
    condition_variable cv;
    std::vector<int> woken;
    auto waiter = [&](int id) -> task<> {
        co_await m.lock();
        std::lock_guard guard(m, std::adopt_lock);
        co_await cv.wait(m);
        woken.push_back(id);
    };
    auto first = owner(waiter(1));
    auto driver = [&]() -> task<std::size_t> {
        cv.notify_one();
        first.cancel();
        co_await yield();
        co_return woken.size();
    };

    auto [cancelled, second, woken_by_one] = run(first, owner(waiter(2)), driver());
    ZEXPECT(cancelled.is_cancelled());
    ZEXPECT(second.has_value());
    ZASSERT(woken_by_one.has_value());
    ZEXPECT(*woken_by_one == 1U);
    ZEXPECT(woken == std::vector{2});
}

// A notified waiter that waits for the mutex again and is cancelled then
// passes the notification on too, and still ends only once it holds the
// mutex.
ZEST_CASE(waiter_cancelled_while_it_waits_for_the_mutex_passes_the_notification_on) {
    mutex m;
    condition_variable cv;
    std::vector<int> woken;
    auto waiter = [&](int id) -> task<> {
        co_await m.lock();
        std::lock_guard guard(m, std::adopt_lock);
        co_await cv.wait(m);
        woken.push_back(id);
    };
    auto first = owner(waiter(1));
    auto driver = [&]() -> task<std::size_t> {
        co_await m.lock();
        // The notified waiter waits for the mutex, and so does the one it
        // passes the notification on to.
        cv.notify_one();
        first.cancel();
        co_await yield();
        auto woken_while_held = woken.size();
        m.unlock();
        co_return woken_while_held;
    };

    auto [cancelled, second, woken_while_held] = run(first, owner(waiter(2)), driver());
    ZEXPECT(cancelled.is_cancelled());
    ZEXPECT(second.has_value());
    ZASSERT(woken_while_held.has_value());
    ZEXPECT(*woken_while_held == 0U);
    ZEXPECT(woken == std::vector{2});
}

};  // ZEST_SUITE(async_runtime_sync_condition_variable)

}  // namespace

}  // namespace kota
