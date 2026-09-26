#include <chrono>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_io_watcher_yield, test::LoopFixture) {

ZEST_CASE(other_tasks_run_first) {
    std::vector<int> order;
    auto yielder = [&]() -> task<> {
        order.push_back(1);
        co_await yield();
        order.push_back(3);
    };
    auto other = [&]() -> task<> {
        order.push_back(2);
        co_return;
    };

    auto [yielded, ran] = run(yielder(), other());
    EXPECT(yielded.has_value());
    EXPECT(order == std::vector{1, 2, 3});
}

// Every resume the current step queued (here the waiter woken by set())
// runs before the yielding task goes on: the hand-over debounced
// cancellation relies on.
ZEST_CASE(queued_resumes_run_first) {
    event ev;
    std::vector<int> order;
    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        order.push_back(1);
    };
    auto setter = [&]() -> task<> {
        ev.set();
        co_await yield();
        order.push_back(2);
    };

    auto [waited, set] = run(waiter(), setter());
    EXPECT(waited.has_value());
    EXPECT(order == std::vector{1, 2});
}

// A yield queued from a timer callback does not resume in that iteration's
// idle phase: a check watcher armed in the same iteration fires first.
ZEST_CASE(from_a_timer_callback_waits_for_the_next_iteration) {
    auto on_check = check::create(loop);
    event go;
    std::vector<int> order;
    auto checker = [&]() -> task<> {
        co_await go.wait();
        on_check.start();
        co_await on_check.wait();
        order.push_back(1);
        on_check.stop();
    };
    auto yielder = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds(1));
        go.set();
        co_await yield();
        order.push_back(2);
    };

    auto [checked, yielded] = run(checker(), yielder());
    EXPECT(checked.has_value());
    EXPECT(order == std::vector{1, 2});
}

ZEST_CASE(can_be_cancelled_while_suspended) {
    bool resumed = false;
    auto yielder = [&]() -> task<> {
        co_await yield();
        resumed = true;
    };
    auto target = yielder();
    auto* node = target.operator->();
    auto cancel_it = [&]() -> task<> {
        node->cancel();
        co_return;
    };

    auto [yielded, driver] = run(std::move(target), cancel_it());
    EXPECT(yielded.is_cancelled());
    EXPECT(!resumed);
}

// A task cancelled while it runs ends at its yield, once the queued yield
// completes.
ZEST_CASE(under_a_cancelled_task_ends_it) {
    async_node* self = nullptr;
    bool resumed = false;
    auto yielder = [&]() -> task<> {
        self->cancel();
        co_await yield();
        resumed = true;
    };
    auto target = yielder();
    self = target.operator->();

    auto [yielded] = run(std::move(target));
    EXPECT(yielded.is_cancelled());
    EXPECT(!resumed);
}

};  // ZEST_SUITE(async_io_watcher_yield)

}  // namespace

}  // namespace kota
