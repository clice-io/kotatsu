// ZEST_SUITE(async_runtime_yield): cooperative yield(loop) scheduling — resumes on the NEXT
// loop iteration after the current drain, cancellation while suspended on a
// yield, the checkpoint on an already-cancelled task, and iteration-spanning
// when enqueued from a timer callback. Direct task<> semantics live in
// task_tests.cpp.
#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_runtime_yield, loop_fixture) {

// yield() resumes on the NEXT loop iteration: every deferred resume produced
// in the current iteration (here: the event waiter woken by set()) runs
// strictly before the yielded task continues. This is the hand-over
// guarantee that debounced-cancellation patterns rely on.
ZEST_CASE(runs_after_current_drain) {
    event ev;
    std::vector<int> order;

    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        order.push_back(1);
    };

    auto driver = [&]() -> task<> {
        co_await sleep(1, loop);
        ev.set();
        co_await yield(loop);
        order.push_back(2);
    };

    auto w = waiter();
    auto d = driver();
    schedule_all(w, d);

    EXPECT(order == (std::vector<int>{1, 2}));
}

// A task suspended on yield() can be cancelled; the queued completion
// delivers the cancellation on the next iteration.
ZEST_CASE(cancel_while_suspended) {
    async_node* worker_node = nullptr;
    bool resumed = false;

    auto worker = [&]() -> task<> {
        co_await yield(loop);
        resumed = true;
    };

    auto canceler = [&]() -> task<> {
        worker_node->cancel();
        co_return;
    };

    auto w = worker();
    auto c = canceler();
    worker_node = w.operator->();
    schedule_all(w, c);

    EXPECT(w->is_cancelled());
    EXPECT(!resumed);
}

// Cancellation checkpoint: a task cancelled while executing that then
// yields finalizes through the yield completion without resuming past it.
ZEST_CASE(checkpoint_on_cancelled_task) {
    async_node* worker_node = nullptr;
    bool resumed = false;

    auto worker = [&]() -> task<> {
        worker_node->cancel();
        co_await yield(loop);
        resumed = true;
    };

    auto w = worker();
    worker_node = w.operator->();
    schedule_all(w);

    EXPECT(w->is_cancelled());
    EXPECT(!resumed);
}

// A yield enqueued from a timer-phase callback must not resume in the same
// iteration's idle phase: callbacks later in the enqueueing iteration (here
// a check-phase watcher armed by a task scheduled alongside) run first.
ZEST_CASE(spans_iteration_from_timer_callback) {
    std::vector<int> order;
    auto chk = check::create(loop);

    auto checker = [&]() -> task<> {
        chk.start();
        co_await chk.wait();
        order.push_back(1);
        chk.stop();
    };

    auto c = checker();

    auto worker = [&]() -> task<> {
        co_await sleep(1, loop);
        loop.schedule(c);
        co_await yield(loop);
        order.push_back(2);
    };

    auto w = worker();
    schedule_all(w);

    EXPECT(order == (std::vector<int>{1, 2}));
}

};  // ZEST_SUITE(async_runtime_yield)

}  // namespace

}  // namespace kota
