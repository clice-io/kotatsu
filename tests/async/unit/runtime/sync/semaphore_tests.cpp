#include <cstddef>
#include <utility>
#include <vector>

#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_runtime_sync_semaphore, zest::LoopFixture) {

ZEST_CASE(try_acquire_takes_available_units) {
    semaphore sem(2);
    EXPECT(sem.try_acquire());
    EXPECT(sem.try_acquire());
    EXPECT(!sem.try_acquire());
    sem.release();
    EXPECT(sem.try_acquire());
}

ZEST_CASE(release_without_waiters_adds_units) {
    semaphore sem;
    sem.release(3);
    EXPECT(sem.try_acquire());
    EXPECT(sem.try_acquire());
    EXPECT(sem.try_acquire());
    EXPECT(!sem.try_acquire());
}

ZEST_CASE(acquire_with_units_left_does_not_suspend) {
    semaphore sem(1);
    std::vector<int> order;
    auto acquirer = [&]() -> task<> {
        co_await sem.acquire();
        order.push_back(1);
    };
    auto other = [&]() -> task<> {
        order.push_back(2);
        co_return;
    };

    auto [acquired, ran] = run(acquirer(), other());
    EXPECT(acquired.has_value());
    EXPECT(ran.has_value());
    EXPECT(order == std::vector{1, 2});
    EXPECT(!sem.try_acquire());
}

ZEST_CASE(release_wakes_waiters_in_order) {
    semaphore sem;
    std::vector<int> order;
    auto waiter = [&](int id) -> task<> {
        co_await sem.acquire();
        order.push_back(id);
    };
    auto releaser = [&]() -> task<std::size_t> {
        sem.release(2);
        co_await yield();
        auto woken_by_two = order.size();
        sem.release();
        co_return woken_by_two;
    };

    auto [first, second, third, woken_by_two] = run(waiter(1), waiter(2), waiter(3), releaser());
    EXPECT(third.has_value());
    ASSERT(woken_by_two.has_value());
    EXPECT(*woken_by_two == 2U);
    EXPECT(order == std::vector{1, 2, 3});
    EXPECT(!sem.try_acquire());
}

ZEST_CASE(cancelled_waiter_leaves_the_queue) {
    semaphore sem;
    bool acquired = false;
    auto waiter = [&]() -> task<> {
        co_await sem.acquire();
        acquired = true;
    };
    auto target = waiter();
    auto cancel_it = [&]() -> task<bool> {
        target.cancel();
        bool queue_empty = !sem.has_waiters();
        sem.release();
        co_return queue_empty;
    };

    auto [waited, queue_empty] = run(target, cancel_it());
    EXPECT(waited.is_cancelled());
    EXPECT(!acquired);
    ASSERT(queue_empty.has_value());
    EXPECT(*queue_empty);
    EXPECT(sem.try_acquire());
}

// release() hands a unit to the first waiter before that waiter runs. A
// waiter cancelled in between passes the unit on instead of losing it.
ZEST_CASE(cancelled_waiter_passes_on_a_handed_over_unit) {
    semaphore sem;
    std::vector<int> acquired;
    auto waiter = [&](int id) -> task<> {
        co_await sem.acquire();
        acquired.push_back(id);
    };
    auto first = waiter(1);
    auto hand_over = [&]() -> task<> {
        sem.release();
        first.cancel();
        co_return;
    };

    auto [cancelled, second, driver] = run(first, waiter(2), hand_over());
    EXPECT(cancelled.is_cancelled());
    EXPECT(second.has_value());
    EXPECT(acquired == std::vector{2});
    EXPECT(!sem.try_acquire());
}

ZEST_CASE(cancelled_last_waiter_returns_a_handed_over_unit) {
    semaphore sem;
    auto waiter = [&]() -> task<> {
        co_await sem.acquire();
    };
    auto target = waiter();
    auto hand_over = [&]() -> task<> {
        sem.release();
        target.cancel();
        co_return;
    };

    auto [cancelled, driver] = run(target, hand_over());
    EXPECT(cancelled.is_cancelled());
    EXPECT(sem.try_acquire());
    EXPECT(!sem.try_acquire());
}

ZEST_CASE(scoped_acquire_holds_a_unit_until_the_guard_goes) {
    semaphore sem(1);
    std::vector<int> order;
    auto holder = [&]() -> task<> {
        auto held = co_await sem.scoped_acquire();
        // Lets the waiter queue up for the unit.
        co_await yield();
        order.push_back(0);
    };
    auto waiter = [&]() -> task<> {
        auto held = co_await sem.scoped_acquire();
        order.push_back(1);
    };

    auto [held, waited] = run(holder(), waiter());
    EXPECT(held.has_value());
    EXPECT(waited.has_value());
    EXPECT(order == std::vector{0, 1});
    EXPECT(sem.try_acquire());
    EXPECT(!sem.try_acquire());
}

// release() gives the unit back before the guard goes, which then leaves the
// count alone; a guard moved from holds nothing either.
ZEST_CASE(guard_releases_once_whether_early_or_moved) {
    semaphore sem(1);

    struct Seen {
        bool back_after_release = false;
        bool held_after_move = false;
        bool back_after_moved_to_goes = false;
    };

    auto use = [&]() -> task<Seen> {
        Seen seen;
        {
            auto held = co_await sem.scoped_acquire();
            held.release();
            seen.back_after_release = sem.try_acquire();
            sem.release();
        }
        auto held = co_await sem.scoped_acquire();
        {
            auto moved = std::move(held);
            seen.held_after_move = !sem.try_acquire();
        }
        seen.back_after_moved_to_goes = sem.try_acquire();
        co_return seen;
    };

    auto [result] = run(use());
    ASSERT(result.has_value());
    EXPECT(result->back_after_release);
    EXPECT(result->held_after_move);
    EXPECT(result->back_after_moved_to_goes);
    // The successful try_acquire() above still holds the one unit.
    EXPECT(!sem.try_acquire());
}

// Assigning over a guard releases the unit it held, and takes the other's.
ZEST_CASE(guard_assigned_over_releases_what_it_held) {
    semaphore sem(2);

    struct Seen {
        bool one_back = false;
        bool none_left = false;
    };

    auto use = [&]() -> task<Seen> {
        Seen seen;
        auto held = co_await sem.scoped_acquire();
        auto other = co_await sem.scoped_acquire();
        held = std::move(other);
        seen.one_back = sem.try_acquire();
        seen.none_left = !sem.try_acquire();
        sem.release();
        co_return seen;
    };

    auto [result] = run(use());
    ASSERT(result.has_value());
    EXPECT(result->one_back);
    EXPECT(result->none_left);
    EXPECT(sem.try_acquire());
    EXPECT(sem.try_acquire());
    EXPECT(!sem.try_acquire());
}

// A scoped_acquire() a cancel ends gives no guard, and a unit handed over to
// it before it resumed goes back.
ZEST_CASE(cancelled_scoped_acquire_gives_no_guard) {
    semaphore sem;
    bool acquired = false;
    auto waiter = [&]() -> task<> {
        auto held = co_await sem.scoped_acquire();
        acquired = true;
    };
    auto waiting = waiter();
    auto handed_over = waiter();
    auto cancel_them = [&]() -> task<> {
        waiting.cancel();
        sem.release();
        handed_over.cancel();
        co_return;
    };

    auto [first, second, driver] = run(waiting, handed_over, cancel_them());
    EXPECT(first.is_cancelled());
    EXPECT(second.is_cancelled());
    EXPECT(!acquired);
    EXPECT(sem.try_acquire());
    EXPECT(!sem.try_acquire());
}

// A task cancelled while it holds a guard keeps the unit until its frame
// goes, which releases it.
ZEST_CASE(guard_releases_when_the_frame_holding_it_goes) {
    semaphore sem(1);
    event gate;
    auto holder = [&]() -> task<> {
        auto held = co_await sem.scoped_acquire();
        co_await gate.wait();
    };
    auto driver = [&]() -> task<std::pair<bool, bool>> {
        auto race = when_any(holder(), yield());
        auto raced = co_await race;
        bool held_after_race = !sem.try_acquire();
        co_return std::pair{raced.index() == 1, held_after_race};
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(result->first);
    EXPECT(result->second);
    EXPECT(sem.try_acquire());
}

};  // ZEST_SUITE(async_runtime_sync_semaphore)

}  // namespace

}  // namespace kota
