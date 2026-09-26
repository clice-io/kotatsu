#include <utility>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_runtime_sync_event, test::LoopFixture) {

ZEST_CASE(set_wakes_every_waiter) {
    event ev;
    int woken = 0;
    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        woken += 1;
    };
    auto setter = [&]() -> task<> {
        ev.set();
        co_return;
    };

    auto [first, second, set] = run(waiter(), waiter(), setter());
    EXPECT(first.has_value());
    EXPECT(second.has_value());
    EXPECT(woken == 2);
    EXPECT(ev.is_set());
}

ZEST_CASE(set_resumes_waiters_after_the_setter_suspends) {
    event ev;
    std::vector<int> order;
    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        order.push_back(2);
    };
    auto setter = [&]() -> task<> {
        ev.set();
        order.push_back(1);
        co_return;
    };

    auto [waited, set] = run(waiter(), setter());
    EXPECT(waited.has_value());
    EXPECT(order == std::vector{1, 2});
}

ZEST_CASE(wait_on_a_set_event_does_not_suspend) {
    event ev(true);
    std::vector<int> order;
    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        order.push_back(1);
    };
    auto other = [&]() -> task<> {
        order.push_back(2);
        co_return;
    };

    auto [first, second] = run(waiter(), other());
    EXPECT(first.has_value());
    EXPECT(order == std::vector{1, 2});
}

ZEST_CASE(reset_makes_waiters_wait_again) {
    event ev(true);
    ev.reset();
    EXPECT(!ev.is_set());
    std::vector<int> order;
    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        order.push_back(2);
    };
    auto setter = [&]() -> task<> {
        order.push_back(1);
        ev.set();
        co_return;
    };

    auto [waited, set] = run(waiter(), setter());
    EXPECT(waited.has_value());
    EXPECT(order == std::vector{1, 2});
}

ZEST_CASE(interrupt_cancels_every_current_waiter) {
    event ev;
    int reached = 0;
    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        reached += 1;
    };
    auto interrupter = [&]() -> task<> {
        ev.interrupt();
        co_return;
    };

    auto [first, second, driver] = run(waiter(), waiter(), interrupter());
    EXPECT(first.is_cancelled());
    EXPECT(second.is_cancelled());
    EXPECT(reached == 0);
    EXPECT(!ev.is_set());
}

// interrupt() reaches only the waiters queued when it is called: an earlier
// interrupt does nothing to a later wait, and a waiter that waits again after
// being interrupted is woken by the next set().
ZEST_CASE(interrupt_leaves_later_waits_alone) {
    event ev;
    ev.interrupt();
    auto waiter = [&]() -> task<std::pair<bool, bool>> {
        auto first = co_await ev.wait().catch_cancel();
        auto second = co_await ev.wait().catch_cancel();
        co_return std::pair{first.is_cancelled(), second.has_value()};
    };
    auto driver = [&]() -> task<> {
        ev.interrupt();
        co_await yield();
        ev.set();
    };

    auto [waited, drove] = run(waiter(), driver());
    ASSERT(waited.has_value());
    EXPECT(waited->first);
    EXPECT(waited->second);
}

ZEST_CASE(interrupt_keeps_the_signaled_state) {
    event unset;
    unset.interrupt();
    EXPECT(!unset.is_set());

    event set(true);
    set.interrupt();
    EXPECT(set.is_set());
}

ZEST_CASE(cancelled_waiter_leaves_the_queue) {
    event ev;
    bool reached = false;
    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        reached = true;
    };
    auto target = waiter();
    auto* node = target.operator->();
    auto cancel_it = [&]() -> task<bool> {
        node->cancel();
        bool queue_empty = ev.get_head() == nullptr;
        ev.interrupt();
        ev.set();
        co_return queue_empty;
    };

    auto [waited, queue_empty] = run(std::move(target), cancel_it());
    EXPECT(waited.is_cancelled());
    EXPECT(!reached);
    ASSERT(queue_empty.has_value());
    EXPECT(*queue_empty);
}

// A task cancelled while it runs stops at the wait instead of queueing on it.
ZEST_CASE(wait_under_a_cancelled_task_does_not_queue) {
    event ev;
    async_node* self = nullptr;
    bool reached = false;
    auto worker = [&]() -> task<> {
        self->cancel();
        co_await event::wait_awaiter(ev);
        reached = true;
    };
    auto target = worker();
    self = target.operator->();

    auto [result] = run(std::move(target));
    EXPECT(result.is_cancelled());
    EXPECT(!reached);
    EXPECT(ev.get_head() == nullptr);
}

ZEST_CASE(sets_chained_through_waiters_all_resume) {
    event first;
    event second;
    event third;
    std::vector<int> order;
    auto relay = [&](event& in, event& out, int id) -> task<> {
        co_await in.wait();
        order.push_back(id);
        out.set();
    };
    auto last = [&]() -> task<> {
        co_await third.wait();
        order.push_back(3);
    };
    auto trigger = [&]() -> task<> {
        first.set();
        co_return;
    };

    auto [end, middle, start, driver] =
        run(last(), relay(second, third, 2), relay(first, second, 1), trigger());
    EXPECT(end.has_value());
    EXPECT(order == std::vector{1, 2, 3});
}

};  // ZEST_SUITE(async_runtime_sync_event)

}  // namespace

}  // namespace kota
