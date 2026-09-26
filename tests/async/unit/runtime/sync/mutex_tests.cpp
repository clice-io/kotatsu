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
    auto* node = target.operator->();
    auto cancel_it = [&]() -> task<bool> {
        node->cancel();
        bool queue_empty = m.get_head() == nullptr;
        m.unlock();
        co_return queue_empty;
    };

    auto [waited, queue_empty] = run(std::move(target), cancel_it());
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
    auto* first_node = first.operator->();
    auto hand_over = [&]() -> task<> {
        m.unlock();
        first_node->cancel();
        co_return;
    };

    auto [cancelled, second, driver] = run(std::move(first), waiter(2), hand_over());
    EXPECT(cancelled.is_cancelled());
    EXPECT(second.has_value());
    EXPECT(acquired == std::vector{2});
    EXPECT(m.try_lock());
}

};  // ZEST_SUITE(async_runtime_sync_mutex)

}  // namespace

}  // namespace kota
