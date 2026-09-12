// TEST_SUITE(when_cancel): regular cancellation semantics for when_all/when_any
// — child self-cancel propagation, parent → children propagation, catch_cancel
// interception, external token cancel, structured completion waiting for
// cancelled children, and the io-path cancellation checkpoint. Reentrant /
// arm-phase / looping cancel scenarios live in reentrancy.cpp.
#include <utility>

#include "../../loop_fixture.h"
#include "../../support.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

TEST_SUITE(when_cancel) {

TEST_CASE(all_child_cancel_propagates) {
    int cancel_started = 0;
    int slow_started = 0;
    int slow_done = 0;

    auto canceler = [&]() -> task<int> {
        cancel_started += 1;
        co_await sleep(1);
        co_await cancel();
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        slow_started += 1;
        co_await sleep(5);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<> {
        co_await when_all(slow(), canceler());
    };

    auto task = combined();
    run(task);

    EXPECT_TRUE(task->is_cancelled());
    EXPECT_EQ(cancel_started, 1);
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(all_pre_cancelled_child) {
    int first_ran = 0;
    int second_ran = 0;

    auto work = [](int& ran) -> task<int> {
        ran += 1;
        co_return 1;
    };

    // A child cancelled before the aggregate arms must neither run nor be
    // "revived" by attach; the cancellation decides the aggregate and the
    // sibling is cancelled before it starts.
    auto combined = [&]() -> task<> {
        auto doomed = work(first_ran);
        doomed->cancel();
        co_await when_all(std::move(doomed), work(second_ran));
    };

    auto task = combined();
    run(task);

    EXPECT_TRUE(task->is_cancelled());
    EXPECT_EQ(first_ran, 0);
    EXPECT_EQ(second_ran, 0);
}

TEST_CASE(any_child_cancel_propagates) {
    int cancel_started = 0;
    int slow_started = 0;
    int slow_done = 0;

    auto canceler = [&]() -> task<int> {
        cancel_started += 1;
        co_await sleep(1);
        co_await cancel();
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        slow_started += 1;
        co_await sleep(5);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<> {
        co_await when_any(slow(), canceler());
    };

    auto task = combined();
    run(task);

    EXPECT_TRUE(task->is_cancelled());
    EXPECT_EQ(cancel_started, 1);
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(any_all_children_cancel) {
    auto canceler = [&]() -> task<int> {
        co_await sleep(1);
        co_await cancel();
        co_return 0;
    };

    auto combined = [&]() -> task<> {
        co_await when_any(canceler(), canceler());
    };

    auto task = combined();
    run(task);

    EXPECT_TRUE(task->is_cancelled());
}

TEST_CASE(all_catch_cancel_captures) {
    int slow_done = 0;

    auto canceler = [&]() -> task<int> {
        co_await sleep(1);
        co_await cancel();
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        co_await sleep(5);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<> {
        auto result = co_await when_all(slow(), canceler().catch_cancel());
        EXPECT_TRUE(result.is_cancelled());
    };

    auto task = combined();
    run(task);

    EXPECT_TRUE(task->is_finished());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(any_catch_cancel_captures) {
    int slow_done = 0;

    auto canceler = [&]() -> task<int> {
        co_await sleep(1);
        co_await cancel();
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        co_await sleep(5);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<> {
        auto result = co_await when_any(slow(), canceler().catch_cancel());
        EXPECT_TRUE(result.is_cancelled());
    };

    auto task = combined();
    run(task);

    EXPECT_TRUE(task->is_finished());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(all_mixed_cancel_intercept) {
    auto normal = [&]() -> task<int> {
        co_await sleep(1);
        co_return 42;
    };

    // Wrap self-cancelling in an intermediate task that handles the cancellation
    auto self_cancelling = [&]() -> task<int> {
        auto inner = []() -> task<int> {
            co_await cancel();
            co_return 0;
        };
        auto result = co_await inner().catch_cancel();
        co_return result.has_value() ? *result : -1;
    };

    auto combined = [&]() -> task<int> {
        auto [a, b] = co_await when_all(normal(), self_cancelling());
        co_return a;
    };

    auto [res] = run(combined());
    EXPECT_EQ(res, 42);
}

TEST_CASE(all_token_cancel) {
    cancellation_source source;
    int finished = 0;

    auto slow1 = [&]() -> task<int> {
        co_await sleep(10);
        finished += 1;
        co_return 1;
    };

    auto slow2 = [&]() -> task<int> {
        co_await sleep(10);
        finished += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<int> {
        auto [a, b] = co_await when_all(slow1(), slow2());
        co_return a + b;
    };

    auto guarded = with_token(combined(), source.token());

    auto canceler = [&]() -> task<> {
        co_await sleep(1);
        source.cancel();
    };

    auto cancel_task = canceler();
    run(guarded, cancel_task);

    EXPECT_FALSE(guarded.value().has_value());
    EXPECT_EQ(finished, 0);
}

TEST_CASE(any_token_cancel) {
    cancellation_source source;
    int finished = 0;

    auto slow1 = [&]() -> task<int> {
        co_await sleep(10);
        finished += 1;
        co_return 1;
    };

    auto slow2 = [&]() -> task<int> {
        co_await sleep(10);
        finished += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(slow1(), slow2());
    };

    auto guarded = with_token(combined(), source.token());

    auto canceler = [&]() -> task<> {
        co_await sleep(1);
        source.cancel();
    };

    auto cancel_task = canceler();
    run(guarded, cancel_task);

    EXPECT_FALSE(guarded.value().has_value());
    EXPECT_EQ(finished, 0);
}

TEST_CASE(all_waits_for_cancelled_children) {
    int op_destroyed = 0;

    auto slow = [&]() -> task<int> {
        deferred_cancel_await op(op_destroyed);
        co_await op;
        co_return 2;
    };

    auto canceler = []() -> task<int> {
        co_await cancel();
        co_return 1;
    };

    auto finisher = []() -> task<> {
        co_await sleep(1);
        deferred_cancel_await::finish_pending_cancel();
    };

    auto combined = [&]() -> task<> {
        co_await when_all(slow(), canceler());
    };

    auto probe = [&]() -> task<> {
        auto res = co_await combined().catch_cancel();
        EXPECT_FALSE(res.has_value());
    };

    auto probe_task = probe();
    auto finisher_task = finisher();
    run(probe_task, finisher_task);
    EXPECT_EQ(op_destroyed, 1);
}

TEST_CASE(any_waits_for_cancelled_children) {
    int op_destroyed = 0;

    auto slow = [&]() -> task<int> {
        deferred_cancel_await op(op_destroyed);
        co_await op;
        co_return 2;
    };

    auto fast = []() -> task<int> {
        co_return 1;
    };

    auto finisher = []() -> task<> {
        co_await sleep(1);
        deferred_cancel_await::finish_pending_cancel();
    };

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(slow(), fast());
    };

    auto task = combined();
    auto finisher_task = finisher();
    run(task, finisher_task);
    EXPECT_TRUE(task->is_finished());
    auto winner = task.result();
    EXPECT_EQ(winner.index(), 1U);
    EXPECT_EQ(std::get<1>(winner), 1);
    EXPECT_EQ(op_destroyed, 1);
}

TEST_CASE(all_sync_cancel) {
    auto canceler = []() -> task<int> {
        co_await cancel();
        co_return 0;
    };

    auto normal = []() -> task<int> {
        co_return 42;
    };

    auto combined = [&]() -> task<> {
        co_await when_all(canceler(), normal());
    };

    auto t = combined();
    run(t);
    EXPECT_TRUE(t->is_cancelled());
}

TEST_CASE(any_sync_cancel) {
    auto canceler = []() -> task<int> {
        co_await cancel();
        co_return 0;
    };

    auto normal = []() -> task<int> {
        co_return 42;
    };

    auto combined = [&]() -> task<> {
        co_await when_any(canceler(), normal());
    };

    auto t = combined();
    run(t);
    EXPECT_TRUE(t->is_cancelled());
}

TEST_CASE(all_parent_cancel_propagates_to_children) {
    int child1_done = 0;
    int child2_done = 0;

    auto child1 = [&]() -> task<int> {
        co_await sleep(10);
        child1_done += 1;
        co_return 1;
    };

    auto child2 = [&]() -> task<int> {
        co_await sleep(10);
        child2_done += 1;
        co_return 2;
    };

    auto parent = [&]() -> task<int> {
        auto [a, b] = co_await when_all(child1(), child2());
        co_return a + b;
    };

    auto canceler = []() -> task<int> {
        co_await sleep(1);
        co_await cancel();
        co_return 0;
    };

    auto outer = [&]() -> task<> {
        co_await when_all(parent(), canceler());
    };

    auto t = outer();
    run(t);

    EXPECT_TRUE(t->is_cancelled());
    EXPECT_EQ(child1_done, 0);
    EXPECT_EQ(child2_done, 0);
}

TEST_CASE(any_parent_cancel_propagates_to_children) {
    int child1_done = 0;
    int child2_done = 0;

    auto child1 = [&]() -> task<int> {
        co_await sleep(10);
        child1_done += 1;
        co_return 1;
    };

    auto child2 = [&]() -> task<int> {
        co_await sleep(10);
        child2_done += 1;
        co_return 2;
    };

    auto parent = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(child1(), child2());
    };

    auto canceler = []() -> task<int> {
        co_await sleep(1);
        co_await cancel();
        co_return 0;
    };

    auto outer = [&]() -> task<> {
        co_await when_all(parent(), canceler());
    };

    auto t = outer();
    run(t);

    EXPECT_TRUE(t->is_cancelled());
    EXPECT_EQ(child1_done, 0);
    EXPECT_EQ(child2_done, 0);
}

// Cancellation checkpoint on the io path: a task cancelled while executing
// that then awaits an io operation has the operation cancelled at the
// suspension point, and finalizes once the (asynchronous) cancel completes.
TEST_CASE(checkpoint_cancels_io_op) {
    int destroyed = 0;
    async_node* worker_node = nullptr;

    auto worker = [&]() -> task<> {
        worker_node->cancel();
        co_await deferred_cancel_await(destroyed);
    };

    auto driver = [&]() -> task<> {
        co_await sleep(1);
        deferred_cancel_await::finish_pending_cancel();
    };

    auto t = worker();
    auto d = driver();
    worker_node = t.operator->();
    run(t, d);

    EXPECT_TRUE(t->is_cancelled());
}

};  // TEST_SUITE(when_cancel)

}  // namespace kota
