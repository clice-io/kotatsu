#include <cstddef>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_runtime_sync_semaphore, test::LoopFixture) {

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

ZEST_CASE(scoped_acquire_guard_releases_when_it_goes) {
    semaphore s(1);
    auto acquirer = [&]() -> task<std::vector<bool>> {
        std::vector<bool> available;
        {
            auto held = co_await s.scoped_acquire();
            available.push_back(s.try_acquire());
        }
        available.push_back(s.try_acquire());
        co_return available;
    };

    auto [result] = run(acquirer());
    ASSERT(result.has_value());
    EXPECT(*result == std::vector{false, true});
}

ZEST_CASE(scoped_acquire_guard_releases_once) {
    semaphore s(1);
    auto acquirer = [&]() -> task<> {
        auto held = co_await s.scoped_acquire();
        auto moved = std::move(held);
        moved.release();
        co_return;
    };

    auto [result] = run(acquirer());
    EXPECT(result.has_value());
    EXPECT(s.try_acquire());
    EXPECT(!s.try_acquire());
}

// A task cancelled while it holds the guard releases the unit once its frame
// goes, as a group's child goes once it has ended: the next waiter gets it.
ZEST_CASE(scoped_acquire_guard_releases_when_its_task_is_cancelled) {
    semaphore s(1);
    event gate;
    cancellation_source stop;
    bool waiter_acquired = false;
    auto holder = [&]() -> task<> {
        auto held = co_await s.scoped_acquire();
        co_await gate.wait();
    };
    auto waiter = [&]() -> task<> {
        auto acquired = co_await s.scoped_acquire();
        waiter_acquired = true;
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
    EXPECT(waiter_acquired);
}

};  // ZEST_SUITE(async_runtime_sync_semaphore)

}  // namespace

}  // namespace kota
