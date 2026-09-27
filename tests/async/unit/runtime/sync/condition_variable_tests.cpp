#include <cstddef>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_runtime_sync_condition_variable, test::LoopFixture) {

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
    ASSERT(held_after_wait.has_value());
    EXPECT(*held_after_wait);
    EXPECT(notified.has_value());
    EXPECT(m.try_lock());
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
    EXPECT(second.has_value());
    ASSERT(woken_by_one.has_value());
    EXPECT(*woken_by_one == 1U);
    EXPECT(order == std::vector{1, 2});
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
    EXPECT(third.has_value());
    EXPECT(woken == 3);
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
    EXPECT(waited.has_value());
    ASSERT(woken_before.has_value());
    EXPECT(!*woken_before);
    EXPECT(woken);
}

ZEST_CASE(cancelled_waiter_leaves_the_queue) {
    mutex m;
    condition_variable cv;
    std::vector<int> woken;
    auto waiter = [&](int id) -> task<> {
        co_await m.lock();
        co_await cv.wait(m);
        woken.push_back(id);
        m.unlock();
    };
    auto first = waiter(1);
    auto* first_node = first.operator->();
    auto driver = [&]() -> task<> {
        first_node->cancel();
        cv.notify_one();
        co_return;
    };

    auto [cancelled, second, drove] = run(std::move(first), waiter(2), driver());
    EXPECT(cancelled.is_cancelled());
    EXPECT(second.has_value());
    EXPECT(woken == std::vector{2});
}

// Cancelled after being notified but before it ran, a waiter never takes the
// mutex back: the caller is cancelled too, so the mutex stays free.
ZEST_CASE(waiter_cancelled_after_notify_leaves_the_mutex_free) {
    mutex m;
    condition_variable cv;
    auto waiter = [&]() -> task<> {
        co_await m.lock();
        co_await cv.wait(m);
        m.unlock();
    };
    auto target = waiter();
    auto* node = target.operator->();
    auto driver = [&]() -> task<> {
        cv.notify_one();
        node->cancel();
        co_return;
    };

    auto [cancelled, drove] = run(std::move(target), driver());
    EXPECT(cancelled.is_cancelled());
    EXPECT(m.try_lock());
}

// Open question, kept to document current behaviour: a notification handed
// to a waiter that is cancelled before it runs is lost, where mutex and
// semaphore pass a handed-over grant on to the next waiter.
ZEST_CASE(notify_one_to_a_waiter_cancelled_before_it_runs_is_lost) {
    mutex m;
    condition_variable cv;
    std::vector<int> woken;
    auto waiter = [&](int id) -> task<> {
        co_await m.lock();
        co_await cv.wait(m);
        woken.push_back(id);
        m.unlock();
    };
    auto first = waiter(1);
    auto* first_node = first.operator->();
    auto second = waiter(2);
    auto* second_node = second.operator->();
    auto driver = [&]() -> task<std::size_t> {
        cv.notify_one();
        first_node->cancel();
        co_await yield();
        auto woken_by_one = woken.size();
        second_node->cancel();
        co_return woken_by_one;
    };

    auto [cancelled, waiting, woken_by_one] = run(std::move(first), std::move(second), driver());
    EXPECT(cancelled.is_cancelled());
    EXPECT(waiting.is_cancelled());
    ASSERT(woken_by_one.has_value());
    EXPECT(*woken_by_one == 0U);
}

};  // ZEST_SUITE(async_runtime_sync_condition_variable)

}  // namespace

}  // namespace kota
