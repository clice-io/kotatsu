#include <utility>
#include <vector>

#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_runtime_sync_mutex, zest::LoopFixture) {

ZEST_CASE(try_lock_takes_a_free_mutex_only) {
    mutex m;
    EXPECT(m.try_lock());
    EXPECT(!m.try_lock());
    m.unlock();
    EXPECT(m.try_lock());
    m.unlock();
}

ZEST_CASE(lock_on_a_free_mutex_does_not_suspend) {
    mutex m;
    std::vector<int> order;
    auto locker = [&]() -> task<> {
        co_await m.lock();
        order.push_back(1);
        m.unlock();
    };
    auto other = [&]() -> task<> {
        order.push_back(2);
        co_return;
    };

    auto [locked, ran] = run(locker(), other());
    EXPECT(locked.has_value());
    EXPECT(ran.has_value());
    EXPECT(order == std::vector{1, 2});
}

ZEST_CASE(unlock_hands_the_mutex_to_waiters_in_order) {
    mutex m;
    std::vector<int> order;
    auto holder = [&]() -> task<> {
        co_await m.lock();
        // Lets the waiters queue up behind the lock.
        co_await yield();
        order.push_back(0);
        m.unlock();
    };
    auto waiter = [&](int id) -> task<> {
        co_await m.lock();
        order.push_back(id);
        m.unlock();
    };

    auto [held, first, second, third] = run(holder(), waiter(1), waiter(2), waiter(3));
    EXPECT(held.has_value());
    EXPECT(third.has_value());
    EXPECT(order == std::vector{0, 1, 2, 3});
    EXPECT(m.try_lock());
}

ZEST_CASE(cancelled_waiter_leaves_the_queue) {
    mutex m;
    ASSERT(m.try_lock());
    bool acquired = false;
    auto waiter = [&]() -> task<> {
        co_await m.lock();
        acquired = true;
        m.unlock();
    };
    auto target = waiter();
    auto cancel_it = [&]() -> task<bool> {
        target.cancel();
        bool queue_empty = !m.has_waiters();
        m.unlock();
        co_return queue_empty;
    };

    auto [waited, queue_empty] = run(target, cancel_it());
    EXPECT(waited.is_cancelled());
    EXPECT(!acquired);
    ASSERT(queue_empty.has_value());
    EXPECT(*queue_empty);
    EXPECT(m.try_lock());
}

// unlock() hands the mutex to the first waiter before that waiter runs. A
// waiter cancelled in between passes the mutex on instead of keeping it.
ZEST_CASE(cancelled_waiter_passes_on_a_handed_over_lock) {
    mutex m;
    ASSERT(m.try_lock());
    std::vector<int> acquired;
    auto waiter = [&](int id) -> task<> {
        co_await m.lock();
        acquired.push_back(id);
        m.unlock();
    };
    auto first = waiter(1);
    auto hand_over = [&]() -> task<> {
        m.unlock();
        first.cancel();
        co_return;
    };

    auto [cancelled, second, driver] = run(first, waiter(2), hand_over());
    EXPECT(cancelled.is_cancelled());
    EXPECT(second.has_value());
    EXPECT(acquired == std::vector{2});
    EXPECT(m.try_lock());
}

// The unlock hands the lock to the waiter and the token's cancel wakes
// with_token, both in one turn: the hand-over, queued first, wins, and the
// mutex is free afterwards either way.
ZEST_CASE(unlock_and_token_cancel_in_one_turn_leave_the_mutex_free) {
    mutex m;
    ASSERT(m.try_lock());
    cancellation_source source;
    auto waiter = [&]() -> task<int> {
        co_await m.lock();
        m.unlock();
        co_return 1;
    };
    auto release = [&]() -> task<> {
        m.unlock();
        source.cancel();
        co_return;
    };

    auto [guarded, driver] = run(with_token(waiter(), source.token()), release());
    ASSERT(guarded.has_value());
    EXPECT(*guarded == 1);
    EXPECT(m.try_lock());
}

ZEST_CASE(scoped_lock_holds_the_mutex_until_the_guard_goes) {
    mutex m;
    std::vector<int> order;
    auto holder = [&]() -> task<> {
        auto held = co_await m.scoped_lock();
        // Lets the waiter queue up behind the lock.
        co_await yield();
        order.push_back(0);
    };
    auto waiter = [&]() -> task<> {
        auto held = co_await m.scoped_lock();
        order.push_back(1);
    };

    auto [held, waited] = run(holder(), waiter());
    EXPECT(held.has_value());
    EXPECT(waited.has_value());
    EXPECT(order == std::vector{0, 1});
    EXPECT(m.try_lock());
}

// unlock() lets the mutex go before the guard does, which then leaves it
// alone; a guard moved from holds nothing either.
ZEST_CASE(guard_unlocks_once_whether_early_or_moved) {
    mutex m;

    struct Seen {
        bool free_after_unlock = false;
        bool held_after_move = false;
        bool free_after_moved_to_goes = false;
    };

    auto use = [&]() -> task<Seen> {
        Seen seen;
        {
            auto held = co_await m.scoped_lock();
            held.unlock();
            seen.free_after_unlock = m.try_lock();
            m.unlock();
        }
        auto held = co_await m.scoped_lock();
        {
            auto moved = std::move(held);
            seen.held_after_move = !m.try_lock();
        }
        seen.free_after_moved_to_goes = m.try_lock();
        co_return seen;
    };

    auto [result] = run(use());
    ASSERT(result.has_value());
    EXPECT(result->free_after_unlock);
    EXPECT(result->held_after_move);
    EXPECT(result->free_after_moved_to_goes);
    // The successful try_lock() above still holds the mutex.
    EXPECT(!m.try_lock());
}

// A scoped_lock() a cancel ends gives no guard: the mutex stays with its
// holder, and nothing unlocks it on the cancelled task's behalf.
ZEST_CASE(cancelled_scoped_lock_gives_no_guard) {
    mutex m;
    ASSERT(m.try_lock());
    bool acquired = false;
    auto waiter = [&]() -> task<> {
        auto held = co_await m.scoped_lock();
        acquired = true;
    };
    auto target = waiter();
    auto cancel_it = [&]() -> task<bool> {
        target.cancel();
        co_return m.has_waiters();
    };

    auto [cancelled, still_queued] = run(target, cancel_it());
    EXPECT(cancelled.is_cancelled());
    ASSERT(still_queued.has_value());
    EXPECT(!*still_queued);
    EXPECT(!acquired);
    EXPECT(!m.try_lock());
    m.unlock();
    EXPECT(m.try_lock());
}

// A task cancelled while it holds a guard ends where the cancel reached it,
// still holding the mutex; the guard unlocks it as the task's frame goes.
ZEST_CASE(guard_unlocks_when_the_frame_holding_it_goes) {
    mutex m;
    event gate;
    auto holder = [&]() -> task<> {
        auto held = co_await m.scoped_lock();
        co_await gate.wait();
    };
    auto driver = [&]() -> task<std::pair<bool, bool>> {
        auto race = when_any(holder(), yield());
        auto raced = co_await race;
        bool held_after_race = !m.try_lock();
        co_return std::pair{raced.index() == 1, held_after_race};
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(result->first);
    EXPECT(result->second);
    EXPECT(m.try_lock());
}

};  // ZEST_SUITE(async_runtime_sync_mutex)

}  // namespace

}  // namespace kota
