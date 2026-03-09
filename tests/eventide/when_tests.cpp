#include <stdexcept>

#include "eventide/zest/zest.h"
#include "eventide/async/cancellation.h"
#include "eventide/async/loop.h"
#include "eventide/async/sync.h"
#include "eventide/async/task.h"
#include "eventide/async/watcher.h"
#include "eventide/async/when.h"

namespace eventide {

namespace {

struct deferred_cancel_await : system_op {
    inline static deferred_cancel_await* pending = nullptr;

    int* destroyed = nullptr;

    explicit deferred_cancel_await(int& destroyed_count) : destroyed(&destroyed_count) {
        assert(pending == nullptr && "only one deferred_cancel_await may be pending at a time");
        action = &on_cancel;
        pending = this;
    }

    deferred_cancel_await(const deferred_cancel_await&) = delete;
    deferred_cancel_await& operator=(const deferred_cancel_await&) = delete;
    deferred_cancel_await(deferred_cancel_await&&) = delete;
    deferred_cancel_await& operator=(deferred_cancel_await&&) = delete;

    ~deferred_cancel_await() {
        if(destroyed) {
            *destroyed += 1;
        }
        if(pending == this) {
            pending = nullptr;
        }
    }

    static void on_cancel(system_op*) {}

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        return this->link_continuation(&waiting.promise(), location);
    }

    void await_resume() const noexcept {}

    static void finish_pending_cancel() {
        auto* op = pending;
        assert(op != nullptr && "finish_pending_cancel requires a pending awaiter");
        pending = nullptr;
        op->complete();
    }
};

TEST_SUITE(when_ops) {

TEST_CASE(when_all_values) {
    auto a = []() -> task<int> {
        co_return 1;
    };

    auto b = []() -> task<int> {
        co_return 2;
    };

    auto combined = [&]() -> task<int> {
        auto [x, y] = co_await when_all(a(), b());
        co_return x + y;
    };

    auto [res] = run(combined());
    EXPECT_EQ(res, 3);
}

TEST_CASE(any_first_wins) {
    int a_count = 0;
    int b_count = 0;

    auto a = [&]() -> task<int> {
        a_count += 1;
        co_return 10;
    };

    auto b = [&]() -> task<int> {
        b_count += 1;
        co_return 20;
    };

    auto combined = [&]() -> task<std::size_t> {
        auto idx = co_await when_any(a(), b());
        co_return idx;
    };

    auto [idx] = run(combined());
    EXPECT_EQ(idx, 0U);
    EXPECT_EQ(a_count, 1);
    EXPECT_EQ(b_count, 0);
}

TEST_CASE(all_sleep_values) {
    event_loop loop;
    int slow_done = 0;
    int fast_done = 0;

    auto slow = [&]() -> task<int> {
        co_await sleep(5, loop);
        slow_done += 1;
        co_return 7;
    };

    auto fast = [&]() -> task<int> {
        co_await sleep(1, loop);
        fast_done += 1;
        co_return 9;
    };

    auto combined = [&]() -> task<int> {
        auto [a, b] = co_await when_all(slow(), fast());
        co_return a + b;
    };

    auto task = combined();
    loop.schedule(task);
    loop.run();

    EXPECT_EQ(task.result(), 16);
    EXPECT_EQ(slow_done, 1);
    EXPECT_EQ(fast_done, 1);
}

TEST_CASE(any_sleep_wins) {
    event_loop loop;
    int fast_done = 0;
    int slow_done = 0;

    auto fast = [&]() -> task<int> {
        co_await sleep(1, loop);
        fast_done += 1;
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        co_await sleep(10, loop);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<std::size_t> {
        auto idx = co_await when_any(fast(), slow());
        co_return idx;
    };

    auto task = combined();
    loop.schedule(task);
    loop.run();

    EXPECT_EQ(task.result(), 0U);
    EXPECT_EQ(fast_done, 1);
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(any_child_cancel) {
    event_loop loop;
    int cancel_started = 0;
    int slow_started = 0;
    int slow_done = 0;

    auto canceler = [&]() -> task<int> {
        cancel_started += 1;
        co_await sleep(1, loop);
        co_await cancel();
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        slow_started += 1;
        co_await sleep(5, loop);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<> {
        co_await when_any(slow(), canceler());
    };

    auto task = combined();
    loop.schedule(task);
    loop.run();

    EXPECT_TRUE(task->is_cancelled());
    EXPECT_EQ(cancel_started, 1);
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(all_cancel_others) {
    event_loop loop;
    int cancel_started = 0;
    int slow_started = 0;
    int slow_done = 0;

    auto canceler = [&]() -> task<int> {
        cancel_started += 1;
        co_await sleep(1, loop);
        co_await cancel();
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        slow_started += 1;
        co_await sleep(5, loop);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<> {
        co_await when_all(slow(), canceler());
    };

    auto task = combined();
    loop.schedule(task);
    loop.run();

    EXPECT_TRUE(task->is_cancelled());
    EXPECT_EQ(cancel_started, 1);
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(any_detaches_cancelled_child_until_quiescent) {
    int op_destroyed = 0;

    auto slow = [&]() -> task<int> {
        deferred_cancel_await op(op_destroyed);
        co_await op;
        co_return 2;
    };

    auto fast = []() -> task<int> {
        co_return 1;
    };

    auto combined = [&]() -> task<std::size_t> {
        auto idx = co_await when_any(slow(), fast());
        co_return idx;
    };

    auto [idx] = run(combined());
    EXPECT_EQ(idx, 1U);
    EXPECT_EQ(op_destroyed, 0);

    deferred_cancel_await::finish_pending_cancel();
    EXPECT_EQ(op_destroyed, 1);
}

TEST_CASE(all_detaches_cancelled_sibling_until_quiescent) {
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

    auto combined = [&]() -> task<> {
        co_await when_all(slow(), canceler());
    };

    auto probe = [&]() -> task<> {
        auto res = co_await combined().catch_cancel();
        EXPECT_FALSE(res.has_value());
    };

    run(probe());
    EXPECT_EQ(op_destroyed, 0);

    deferred_cancel_await::finish_pending_cancel();
    EXPECT_EQ(op_destroyed, 1);
}

TEST_CASE(when_all_token_cancel) {
    event_loop loop;
    cancellation_source source;
    int finished = 0;

    auto slow1 = [&]() -> task<int> {
        co_await sleep(10, loop);
        finished += 1;
        co_return 1;
    };

    auto slow2 = [&]() -> task<int> {
        co_await sleep(10, loop);
        finished += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<int> {
        auto [a, b] = co_await when_all(slow1(), slow2());
        co_return a + b;
    };

    auto guarded = with_token(combined(), source.token());

    auto canceler = [&]() -> task<> {
        co_await sleep(1, loop);
        source.cancel();
    };

    auto cancel_task = canceler();

    loop.schedule(guarded);
    loop.schedule(cancel_task);
    loop.run();

    EXPECT_FALSE(guarded.value().has_value());
    EXPECT_EQ(finished, 0);
}

TEST_CASE(when_any_token_cancel) {
    event_loop loop;
    cancellation_source source;
    int finished = 0;

    auto slow1 = [&]() -> task<int> {
        co_await sleep(10, loop);
        finished += 1;
        co_return 1;
    };

    auto slow2 = [&]() -> task<int> {
        co_await sleep(10, loop);
        finished += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<std::size_t> {
        auto idx = co_await when_any(slow1(), slow2());
        co_return idx;
    };

    auto guarded = with_token(combined(), source.token());

    auto canceler = [&]() -> task<> {
        co_await sleep(1, loop);
        source.cancel();
    };

    auto cancel_task = canceler();

    loop.schedule(guarded);
    loop.schedule(cancel_task);
    loop.run();

    EXPECT_FALSE(guarded.value().has_value());
    EXPECT_EQ(finished, 0);
}

TEST_CASE(when_all_mixed_cancel_intercept) {
    event_loop loop;

    auto normal = [&]() -> task<int> {
        co_await sleep(1, loop);
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

    auto task = combined();
    loop.schedule(task);
    loop.run();

    EXPECT_TRUE(task->is_finished());
    EXPECT_EQ(task.result(), 42);
}

TEST_CASE(when_all_single) {
    auto a = []() -> task<int> {
        co_return 42;
    };

    auto combined = [&]() -> task<int> {
        auto [x] = co_await when_all(a());
        co_return x;
    };

    auto [res] = run(combined());
    EXPECT_EQ(res, 42);
}

TEST_CASE(when_all_void) {
    int count = 0;

    auto a = [&]() -> task<> {
        count += 1;
        co_return;
    };

    auto b = [&]() -> task<> {
        count += 10;
        co_return;
    };

    auto combined = [&]() -> task<> {
        co_await when_all(a(), b());
    };

    run(combined());
    EXPECT_EQ(count, 11);
}

TEST_CASE(when_all_immediate) {
    auto a = []() -> task<int> {
        co_return 1;
    };

    auto b = []() -> task<int> {
        co_return 2;
    };

    auto c = []() -> task<int> {
        co_return 3;
    };

    auto combined = [&]() -> task<int> {
        auto [x, y, z] = co_await when_all(a(), b(), c());
        co_return x + y + z;
    };

    auto [res] = run(combined());
    EXPECT_EQ(res, 6);
}

TEST_CASE(when_any_single) {
    auto a = []() -> task<int> {
        co_return 99;
    };

    auto combined = [&]() -> task<std::size_t> {
        auto idx = co_await when_any(a());
        co_return idx;
    };

    auto [idx] = run(combined());
    EXPECT_EQ(idx, 0U);
}

TEST_CASE(when_any_second_wins) {
    event_loop loop;

    auto slow = [&]() -> task<int> {
        co_await sleep(10, loop);
        co_return 1;
    };

    auto fast = [&]() -> task<int> {
        co_await sleep(1, loop);
        co_return 2;
    };

    auto combined = [&]() -> task<std::size_t> {
        auto idx = co_await when_any(slow(), fast());
        co_return idx;
    };

    auto task = combined();
    loop.schedule(task);
    loop.run();

    EXPECT_EQ(task.result(), 1U);
}

#ifdef __cpp_exceptions
TEST_CASE(when_all_exception) {
    auto thrower = []() -> task<int> {
        throw std::runtime_error("boom");
        co_return 0;
    };

    auto normal = []() -> task<int> {
        co_return 42;
    };

    auto combined = [&]() -> task<int> {
        auto [a, b] = co_await when_all(thrower(), normal());
        co_return a + b;
    };

    EXPECT_THROWS(run(combined()));
}
#endif

TEST_CASE(when_any_all_cancel) {
    event_loop loop;

    auto canceler = [&]() -> task<int> {
        co_await sleep(1, loop);
        co_await cancel();
        co_return 0;
    };

    auto combined = [&]() -> task<> {
        co_await when_any(canceler(), canceler());
    };

    auto task = combined();
    loop.schedule(task);
    loop.run();

    EXPECT_TRUE(task->is_cancelled());
}

};  // TEST_SUITE(when_ops)

TEST_SUITE(async_scope) {

TEST_CASE(scope_basic) {
    int count = 0;

    auto work = [&](int val) -> task<> {
        count += val;
        co_return;
    };

    auto driver = [&]() -> task<> {
        async_scope scope;
        scope.spawn(work(1));
        scope.spawn(work(10));
        scope.spawn(work(100));
        co_await scope;
    };

    run(driver());
    EXPECT_EQ(count, 111);
}

TEST_CASE(scope_with_sleep) {
    event_loop loop;
    int count = 0;

    auto work = [&](int val, int ms) -> task<> {
        co_await sleep(ms, loop);
        count += val;
    };

    auto driver = [&]() -> task<> {
        async_scope scope;
        scope.spawn(work(1, 5));
        scope.spawn(work(10, 1));
        scope.spawn(work(100, 3));
        co_await scope;
    };

    auto t = driver();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(count, 111);
}

TEST_CASE(scope_child_cancel) {
    event_loop loop;
    int slow_done = 0;

    auto canceler = [&]() -> task<> {
        co_await sleep(1, loop);
        co_await cancel();
    };

    auto slow = [&]() -> task<> {
        co_await sleep(10, loop);
        slow_done += 1;
    };

    auto driver = [&]() -> task<> {
        async_scope scope;
        scope.spawn(slow());
        scope.spawn(canceler());
        co_await scope;
    };

    auto t = driver();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_cancelled());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(scope_token_cancel) {
    event_loop loop;
    cancellation_source source;
    int finished = 0;

    auto slow = [&](int ms) -> task<> {
        co_await sleep(ms, loop);
        finished += 1;
    };

    auto driver = [&]() -> task<int> {
        async_scope scope;
        scope.spawn(slow(10));
        scope.spawn(slow(10));
        co_await scope;
        co_return 1;
    };

    auto guarded = with_token(driver(), source.token());

    auto canceler = [&]() -> task<> {
        co_await sleep(1, loop);
        source.cancel();
    };

    auto cancel_task = canceler();

    loop.schedule(guarded);
    loop.schedule(cancel_task);
    loop.run();

    EXPECT_FALSE(guarded.value().has_value());
    EXPECT_EQ(finished, 0);
}

TEST_CASE(scope_detaches_cancelled_child_until_quiescent) {
    int op_destroyed = 0;

    auto slow = [&]() -> task<> {
        deferred_cancel_await op(op_destroyed);
        co_await op;
    };

    auto canceler = []() -> task<> {
        co_await cancel();
    };

    auto driver = [&]() -> task<> {
        async_scope scope;
        scope.spawn(slow());
        scope.spawn(canceler());
        co_await scope;
    };

    auto probe = [&]() -> task<> {
        auto res = co_await driver().catch_cancel();
        EXPECT_FALSE(res.has_value());
    };

    run(probe());
    EXPECT_EQ(op_destroyed, 0);

    deferred_cancel_await::finish_pending_cancel();
    EXPECT_EQ(op_destroyed, 1);
}

TEST_CASE(scope_empty) {
    auto driver = []() -> task<> {
        async_scope scope;
        co_await scope;
    };

    run(driver());
}

TEST_CASE(scope_not_awaited) {
    int count = 0;

    auto work = [&]() -> task<> {
        count += 1;
        co_return;
    };

    {
        async_scope scope;
        scope.spawn(work());
        scope.spawn(work());
        // scope destroyed without co_await — should not crash or leak
    }

    // Tasks were never resumed, count stays 0
    EXPECT_EQ(count, 0);
}

TEST_CASE(scope_in_when_all) {
    event_loop loop;
    int scope_count = 0;

    auto scoped_work = [&]() -> task<int> {
        async_scope scope;
        auto work = [&]() -> task<> {
            co_await sleep(1, loop);
            scope_count += 1;
        };
        for(int i = 0; i < 3; ++i) {
            scope.spawn(work());
        }
        co_await scope;
        co_return scope_count;
    };

    auto normal = [&]() -> task<int> {
        co_await sleep(1, loop);
        co_return 100;
    };

    auto combined = [&]() -> task<int> {
        auto [a, b] = co_await when_all(scoped_work(), normal());
        co_return a + b;
    };

    auto t = combined();
    loop.schedule(t);
    loop.run();

    EXPECT_EQ(scope_count, 3);
    EXPECT_EQ(t.result(), 103);
}

TEST_CASE(when_all_in_scope) {
    event_loop loop;
    int count = 0;

    auto pair_work = [&]() -> task<> {
        auto a = [&]() -> task<int> {
            co_await sleep(1, loop);
            co_return 1;
        };
        auto b = [&]() -> task<int> {
            co_await sleep(1, loop);
            co_return 2;
        };
        auto [x, y] = co_await when_all(a(), b());
        count += x + y;
    };

    auto driver = [&]() -> task<> {
        async_scope scope;
        scope.spawn(pair_work());
        scope.spawn(pair_work());
        co_await scope;
    };

    auto t = driver();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(count, 6);
}

};  // TEST_SUITE(async_scope)

TEST_SUITE(with_timeout) {

TEST_CASE(completes_before_timeout) {
    event_loop loop;

    auto work = [&]() -> task<int> {
        co_await sleep(1, loop);
        co_return 42;
    };

    auto driver = [&]() -> task<> {
        auto result = co_await with_timeout(work(), 100, loop);
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(*result, 42);
    };

    auto t = driver();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_finished());
}

TEST_CASE(times_out) {
    event_loop loop;

    auto slow = [&]() -> task<int> {
        co_await sleep(100, loop);
        co_return 42;
    };

    auto driver = [&]() -> task<> {
        auto result = co_await with_timeout(slow(), 1, loop);
        EXPECT_FALSE(result.has_value());
    };

    auto t = driver();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_cancelled());
}

TEST_CASE(timeout_void_task) {
    event_loop loop;
    int done = 0;

    auto work = [&]() -> task<> {
        co_await sleep(1, loop);
        done = 1;
    };

    auto driver = [&]() -> task<> {
        auto result = co_await with_timeout(work(), 100, loop);
        EXPECT_TRUE(result.has_value());
    };

    auto t = driver();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(done, 1);
}

TEST_CASE(timeout_void_task_expires) {
    event_loop loop;
    int done = 0;

    auto slow = [&]() -> task<> {
        co_await sleep(100, loop);
        done = 1;
    };

    auto driver = [&]() -> task<> {
        auto result = co_await with_timeout(slow(), 1, loop);
        EXPECT_FALSE(result.has_value());
    };

    auto t = driver();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_cancelled());
    EXPECT_EQ(done, 0);
}

TEST_CASE(timeout_self_cancel) {
    event_loop loop;

    auto self_cancelling = [&]() -> task<int> {
        co_await sleep(1, loop);
        co_await cancel();
        co_return 0;
    };

    auto driver = [&]() -> task<> {
        auto result = co_await with_timeout(self_cancelling(), 100, loop);
        EXPECT_FALSE(result.has_value());
    };

    auto t = driver();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_cancelled());
}

TEST_CASE(timeout_with_token) {
    event_loop loop;
    cancellation_source source;

    auto slow = [&]() -> task<int> {
        co_await sleep(100, loop);
        co_return 42;
    };

    auto driver = [&]() -> task<> {
        auto result = co_await with_token(with_timeout(slow(), 50, loop), source.token());
        EXPECT_FALSE(result.has_value());
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(1, loop);
        source.cancel();
    };

    auto t = driver();
    auto cancel_task = canceler();
    loop.schedule(t);
    loop.schedule(cancel_task);
    loop.run();

    EXPECT_TRUE(t->is_cancelled());
}

};  // TEST_SUITE(with_timeout)

}  // namespace

}  // namespace eventide
