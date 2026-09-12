// TEST_SUITE(task_group_cancel): cancellation semantics for task_group — child
// self-cancel fail-fast, external token cancel, group.abort() (idempotent,
// empty, while join suspended, from a running child), spawn-after-cancel, and
// partial completion then cancel. Error/exception collection under cancel lives
// in errors.cpp; frame lifetime in lifetime.cpp.
#include "../../loop_fixture.h"
#include "../../support.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

TEST_SUITE(task_group_cancel, loop_fixture) {

TEST_CASE(child_self_cancel) {
    int slow_done = 0;

    auto canceler = [&]() -> task<> {
        co_await sleep(1, loop);
        co_await cancel();
    };

    auto slow = [&]() -> task<> {
        co_await sleep(5, loop);
        slow_done += 1;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(canceler());
        group.spawn(slow());
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
    // Child self-cancel triggers fail-fast (cancels siblings) but the
    // group itself finishes normally — InterceptCancel on spawned
    // children means cancellation doesn't propagate as group failure.
    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(token_cancel) {
    cancellation_source source;
    int finished = 0;

    auto slow = [&](int ms) -> task<> {
        co_await sleep(ms, loop);
        finished += 1;
    };

    auto driver = [&]() -> task<int> {
        task_group<> group(loop);
        group.spawn(slow(10));
        group.spawn(slow(10));
        co_await group.join();
        co_return 1;
    };

    auto guarded = with_token(driver(), source.token());

    auto canceler = [&]() -> task<> {
        co_await sleep(1, loop);
        source.cancel();
    };

    auto cancel_task = canceler();
    schedule_all(guarded, cancel_task);

    EXPECT_FALSE(guarded.value().has_value());
    EXPECT_EQ(finished, 0);
}

TEST_CASE(cancel) {
    int finished = 0;

    auto slow = [&](int ms) -> task<> {
        co_await sleep(ms, loop);
        finished += 1;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(slow(50));
        group.spawn(slow(50));
        group.abort();
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
    EXPECT_EQ(finished, 0);
}

TEST_CASE(spawn_after_cancel) {
    int count = 0;

    auto work = [&]() -> task<> {
        count += 1;
        co_return;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(work());
        group.abort();
        group.spawn(work());
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
    EXPECT_EQ(count, 1);
}

TEST_CASE(cancel_idempotent) {
    int finished = 0;

    auto slow = [&]() -> task<> {
        co_await sleep(50, loop);
        finished += 1;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(slow());
        group.abort();
        group.abort();
        group.abort();
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
    EXPECT_EQ(finished, 0);
}

TEST_CASE(empty_cancel) {
    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.abort();
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
    EXPECT_TRUE(t->is_finished());
}

TEST_CASE(external_cancel_sets_stopped) {
    cancellation_source source;
    int finished = 0;

    auto slow = [&](int ms) -> task<> {
        co_await sleep(ms, loop);
        finished += 1;
    };

    auto driver = [&]() -> task<int> {
        task_group<> group(loop);
        group.spawn(slow(50));
        group.spawn(slow(50));
        co_await group.join();
        co_return 1;
    };

    auto guarded = with_token(driver(), source.token());

    auto canceler = [&]() -> task<> {
        co_await sleep(1, loop);
        source.cancel();
    };

    auto cancel_task = canceler();
    schedule_all(guarded, cancel_task);
    EXPECT_FALSE(guarded.value().has_value());
    EXPECT_EQ(finished, 0);
}

// cancel() called while join() is suspended — tests flush_deferred() fast path
TEST_CASE(cancel_while_join_suspended) {
    int finished = 0;
    task_group<>* group_ptr = nullptr;

    auto slow = [&]() -> task<> {
        co_await sleep(std::chrono::seconds(10), loop);
        finished += 1;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group_ptr = &group;
        group.spawn(slow());
        group.spawn(slow());
        co_await group.join();
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(1, loop);
        group_ptr->abort();
    };

    auto t = driver();
    auto c = canceler();
    schedule_all(t, c);
    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(finished, 0);
}

// Partial completion then external cancel then join completes
TEST_CASE(partial_completion_then_cancel) {
    int fast_done = 0;
    int slow_done = 0;
    task_group<>* group_ptr = nullptr;

    auto fast = [&]() -> task<> {
        co_await sleep(1, loop);
        fast_done += 1;
    };

    auto slow = [&]() -> task<> {
        co_await sleep(std::chrono::seconds(10), loop);
        slow_done += 1;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group_ptr = &group;
        group.spawn(fast());
        group.spawn(slow());
        co_await group.join();
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(5, loop);
        group_ptr->abort();
    };

    auto t = driver();
    auto c = canceler();
    schedule_all(t, c);
    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(fast_done, 1);
    EXPECT_EQ(slow_done, 0);
}

// A child task calls group.abort() while running on the call stack.
// Without the child==self sentinel in cancel(), this causes double-finalize:
// cancel() finalizes the running child immediately (child==nullptr, parent!=nullptr),
// then when the coroutine reaches final_suspend, transition_await calls finalize() again.
TEST_CASE(cancel_from_running_child) {
    int slow_done = 0;
    task_group<>* group_ptr = nullptr;

    auto canceler = [&]() -> task<> {
        co_await sleep(1, loop);
        group_ptr->abort();
    };

    auto slow = [&]() -> task<> {
        co_await sleep(100, loop);
        slow_done += 1;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group_ptr = &group;
        group.spawn(canceler());
        group.spawn(slow());
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(slow_done, 0);
}

};  // TEST_SUITE(task_group_cancel)

}  // namespace kota
