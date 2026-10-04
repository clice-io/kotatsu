#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_runtime_sync_mutex, test::LoopFixture) {

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

ZEST_CASE(scoped_lock_guard_unlocks_when_it_goes) {
    mutex m;
    auto locker = [&]() -> task<std::vector<bool>> {
        std::vector<bool> free;
        {
            auto held = co_await m.scoped_lock();
            free.push_back(m.try_lock());
        }
        free.push_back(m.try_lock());
        co_return free;
    };

    auto [result] = run(locker());
    ASSERT(result.has_value());
    EXPECT(*result == std::vector{false, true});
    m.unlock();
}

ZEST_CASE(scoped_lock_guard_unlocks_once) {
    mutex m;
    auto locker = [&]() -> task<bool> {
        auto held = co_await m.scoped_lock();
        auto moved = std::move(held);
        moved.unlock();
        // A second lock, which neither guard may unlock when it goes.
        co_return m.try_lock();
    };

    auto [result] = run(locker());
    ASSERT(result.has_value());
    EXPECT(*result);
    EXPECT(!m.try_lock());
    m.unlock();
}

ZEST_CASE(scoped_lock_guard_assigned_over_unlocks_what_it_held) {
    mutex first;
    mutex second;
    auto locker = [&]() -> task<std::vector<bool>> {
        auto held = co_await first.scoped_lock();
        auto other = co_await second.scoped_lock();
        held = std::move(other);
        std::vector<bool> free{first.try_lock(), second.try_lock()};
        co_return free;
    };

    auto [result] = run(locker());
    ASSERT(result.has_value());
    EXPECT(*result == std::vector{true, false});
    first.unlock();
    EXPECT(second.try_lock());
    second.unlock();
}

// A wait for the lock that a cancel ends gives no guard, and leaves the
// mutex to its holder.
ZEST_CASE(scoped_lock_cancelled_while_waiting_takes_nothing) {
    mutex m;
    cancellation_source stop;
    bool waiter_locked = false;
    auto waiter = [&]() -> task<> {
        auto locked = co_await m.scoped_lock();
        waiter_locked = true;
    };
    auto driver = [&]() -> task<bool> {
        auto held = co_await m.scoped_lock();
        task_group<> group;
        group.spawn(with_token(waiter(), stop.token()));
        stop.cancel();
        co_await group.join();
        held.unlock();
        co_return m.try_lock();
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(*result);
    EXPECT(!waiter_locked);
    m.unlock();
}

// A task cancelled while it holds the guard unlocks once its frame goes, as
// a group's child goes once it has ended: the next waiter gets the mutex.
ZEST_CASE(scoped_lock_guard_unlocks_when_its_task_is_cancelled) {
    mutex m;
    event gate;
    cancellation_source stop;
    bool waiter_locked = false;
    auto holder = [&]() -> task<> {
        auto held = co_await m.scoped_lock();
        co_await gate.wait();
    };
    auto waiter = [&]() -> task<> {
        auto locked = co_await m.scoped_lock();
        waiter_locked = true;
    };
    auto driver = [&]() -> task<> {
        task_group<> group;
        group.spawn(with_token(holder(), stop.token()));
        group.spawn(waiter());
        stop.cancel();
        co_await group.join();
    };

    auto [result] = run(driver());
    EXPECT(result.has_value());
    EXPECT(waiter_locked);
}

};  // ZEST_SUITE(async_runtime_sync_mutex)

}  // namespace

}  // namespace kota
