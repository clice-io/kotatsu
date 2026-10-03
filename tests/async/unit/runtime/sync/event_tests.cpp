#include <memory>
#include <utility>
#include <vector>

#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_runtime_sync_event, zest::LoopFixture) {

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

// A set event stays set: the first wait does not use it up for the second.
ZEST_CASE(wait_on_a_set_event_does_not_suspend) {
    event ev(true);
    std::vector<int> order;
    auto waiter = [&](int id) -> task<> {
        co_await ev.wait();
        order.push_back(id);
    };
    auto other = [&]() -> task<> {
        order.push_back(3);
        co_return;
    };

    auto [first, second, third] = run(waiter(1), waiter(2), other());
    EXPECT(first.has_value());
    EXPECT(second.has_value());
    EXPECT(order == std::vector{1, 2, 3});
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

ZEST_CASE(cancelled_waiter_leaves_the_queue) {
    event ev;
    bool reached = false;
    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        reached = true;
    };
    auto target = waiter();
    auto cancel_it = [&]() -> task<bool> {
        target.cancel();
        bool queue_empty = !ev.has_waiters();
        ev.set();
        co_return queue_empty;
    };

    auto [waited, queue_empty] = run(target, cancel_it());
    EXPECT(waited.is_cancelled());
    EXPECT(!reached);
    ASSERT(queue_empty.has_value());
    EXPECT(*queue_empty);
}

// A task cancelled while it runs stops at the wait instead of queueing on it.
ZEST_CASE(wait_under_a_cancelled_task_does_not_queue) {
    event ev;
    task<> target;
    bool reached = false;
    auto worker = [&]() -> task<> {
        target.cancel();
        co_await ev.wait();
        reached = true;
    };
    target = worker();

    auto [result] = run(target);
    EXPECT(result.is_cancelled());
    EXPECT(!reached);
    EXPECT(!ev.has_waiters());
}

// set() wakes a waiter before it runs; one cancelled in between ends
// cancelled, and the event stays set for the others.
ZEST_CASE(waiter_cancelled_after_set_ends_cancelled) {
    event ev;
    int reached = 0;
    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        reached += 1;
    };
    auto target = waiter();
    auto set_then_cancel = [&]() -> task<> {
        ev.set();
        target.cancel();
        co_return;
    };

    auto [cancelled, other, driver] = run(target, waiter(), set_then_cancel());
    EXPECT(cancelled.is_cancelled());
    EXPECT(other.has_value());
    EXPECT(reached == 1);
    EXPECT(ev.is_set());
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

// An event may go once it is set: a waiter it woke and that is cancelled
// before it resumes has nothing to hand back to it. The sanitizer builds
// catch a read of the freed event.
ZEST_CASE(waiter_cancelled_after_its_event_is_gone_ends_cancelled) {
    auto ev = std::make_unique<event>();
    bool reached = false;
    auto waiter = [&]() -> task<> {
        co_await ev->wait();
        reached = true;
    };
    auto target = waiter();
    auto set_drop_cancel = [&]() -> task<> {
        ev->set();
        ev.reset();
        target.cancel();
        co_return;
    };

    auto [cancelled, driver] = run(target, set_drop_cancel());
    EXPECT(cancelled.is_cancelled());
    EXPECT(!reached);
}

// A set() outside a running loop, once run() has returned, queues the waiter
// on the loop it waits on, which resumes it when it runs again.
ZEST_CASE(set_after_the_loop_returned_resumes_the_waiter_on_its_next_run) {
    event ev;
    bool reached = false;
    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        reached = true;
    };
    auto target = waiter();

    loop.schedule(target);
    // A task waiting on an event keeps no loop running.
    EXPECT(loop.run() == 0);
    ASSERT(!target.done());
    ev.set();
    EXPECT(!reached);
    EXPECT(loop.run() == 0);
    EXPECT(reached);
    EXPECT(target.done());
}

// Waiters a set() outside any task wakes resume in the order they were
// woken, with those that a resumed one wakes after them: a relay's callback
// sets `first`, and the waiter it resumes first sets `second`.
ZEST_CASE(waiters_woken_outside_a_task_resume_in_the_order_they_were_woken) {
    event first;
    event second;
    std::vector<int> order;
    auto setter = [&]() -> task<> {
        co_await first.wait();
        order.push_back(1);
        second.set();
    };
    auto waiter = [&](event& ev, int id) -> task<> {
        co_await ev.wait();
        order.push_back(id);
    };
    auto wake = loop.create_relay();
    auto send = [&]() -> task<> {
        wake.send([&] { first.set(); });
        co_return;
    };

    auto [set, woken, woken_after, sent] =
        run(setter(), waiter(first, 2), waiter(second, 3), send());
    EXPECT(woken_after.has_value());
    EXPECT(order == std::vector{1, 2, 3});
}

};  // ZEST_SUITE(async_runtime_sync_event)

}  // namespace

}  // namespace kota
