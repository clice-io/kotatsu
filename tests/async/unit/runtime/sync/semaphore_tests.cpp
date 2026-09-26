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
    auto* node = target.operator->();
    auto cancel_it = [&]() -> task<bool> {
        node->cancel();
        bool queue_empty = sem.get_head() == nullptr;
        sem.release();
        co_return queue_empty;
    };

    auto [waited, queue_empty] = run(std::move(target), cancel_it());
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
    auto* first_node = first.operator->();
    auto hand_over = [&]() -> task<> {
        sem.release();
        first_node->cancel();
        co_return;
    };

    auto [cancelled, second, driver] = run(std::move(first), waiter(2), hand_over());
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
    auto* node = target.operator->();
    auto hand_over = [&]() -> task<> {
        sem.release();
        node->cancel();
        co_return;
    };

    auto [cancelled, driver] = run(std::move(target), hand_over());
    EXPECT(cancelled.is_cancelled());
    EXPECT(sem.try_acquire());
    EXPECT(!sem.try_acquire());
}

};  // ZEST_SUITE(async_runtime_sync_semaphore)

}  // namespace

}  // namespace kota
