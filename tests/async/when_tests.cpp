#include <stdexcept>

#include "eventide/zest/zest.h"
#include "eventide/async/async.h"

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

struct custom_error {
    int code = 0;

    friend bool operator==(const custom_error&, const custom_error&) = default;
};

template <typename Scope, typename TaskType>
concept scope_spawnable = requires(Scope scope, TaskType task) { scope.spawn(std::move(task)); };

task<int> ready_int(int value) {
    co_return value;
}

task<> ready_void() {
    co_return;
}

task<int> delayed_int(event_loop& loop, int ms, int value) {
    co_await sleep(std::chrono::milliseconds{ms}, loop);
    co_return value;
}

static_assert(detail::owned_awaitable<task<int>>);
static_assert(!detail::owned_awaitable<task<int>&>);
static_assert(detail::owned_awaitable<semaphore::acquire_awaiter>);
static_assert(!detail::owned_awaitable<semaphore::acquire_awaiter&>);
static_assert(detail::async_range<small_vector<task<int>>>);
static_assert(!std::constructible_from<when_any<>>);

void when_type_checks() {
    task<int> value;
    task<> unit;
    task<int, error> value_error;
    task<int, error> other_value_error;
    task<int, custom_error> value_custom_error;
    task<int, error, cancellation> value_error_cancel;

    static_assert(std::same_as<decltype(when_all(std::move(value), std::move(unit)).await_resume()),
                               std::tuple<int, std::nullopt_t>>);
    static_assert(std::same_as<decltype(when_any(std::move(value), std::move(unit)).await_resume()),
                               std::variant<int, std::nullopt_t>>);
    static_assert(
        std::same_as<decltype(when_all(std::move(value_error), std::move(unit)).await_resume()),
                     outcome<std::tuple<int, std::nullopt_t>, error, void>>);
    static_assert(
        std::same_as<decltype(when_any(std::move(value_error), std::move(unit)).await_resume()),
                     outcome<std::variant<int, std::nullopt_t>, error, void>>);
    static_assert(
        std::same_as<
            decltype(when_all(std::move(value_error), std::move(other_value_error)).await_resume()),
            outcome<std::tuple<int, int>, error, void>>);
    static_assert(
        std::same_as<
            decltype(when_any(std::move(value_error), std::move(other_value_error)).await_resume()),
            outcome<std::variant<int, int>, error, void>>);
    static_assert(
        std::same_as<decltype(when_all(std::move(value_error), std::move(value_custom_error))
                                  .await_resume()),
                     outcome<std::tuple<int, int>, std::variant<error, custom_error>, void>>);
    static_assert(
        std::same_as<decltype(when_any(std::move(value_error), std::move(value_custom_error))
                                  .await_resume()),
                     outcome<std::variant<int, int>, std::variant<error, custom_error>, void>>);
    static_assert(std::same_as<
                  decltype(when_all(std::move(value_error_cancel), std::move(unit)).await_resume()),
                  outcome<std::tuple<int, std::nullopt_t>, error, cancellation>>);
    static_assert(std::same_as<
                  decltype(when_any(std::move(value_error_cancel), std::move(unit)).await_resume()),
                  outcome<std::variant<int, std::nullopt_t>, error, cancellation>>);
}

static_assert(std::same_as<decltype(std::declval<async_scope<error>>().await_resume()),
                           outcome<void, error, void>>);
static_assert(scope_spawnable<async_scope<error>, task<int, error>>);
static_assert(!scope_spawnable<async_scope<>, task<int, error>>);

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

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(a(), b());
    };

    auto [winner] = run(combined());
    EXPECT_TRUE(winner.has_value());
    EXPECT_EQ(winner->index(), 0U);
    EXPECT_EQ(std::get<0>(*winner), 10);
    EXPECT_EQ(a_count, 1);
    EXPECT_EQ(b_count, 0);
}

TEST_CASE(all_sleep_values) {
    event_loop loop;
    int slow_done = 0;
    int fast_done = 0;

    auto slow = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{5}, loop);
        slow_done += 1;
        co_return 7;
    };

    auto fast = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
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
        co_await sleep(std::chrono::milliseconds{1}, loop);
        fast_done += 1;
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{10}, loop);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(fast(), slow());
    };

    auto task = combined();
    loop.schedule(task);
    loop.run();

    auto winner = task.result();
    EXPECT_EQ(winner.index(), 0U);
    EXPECT_EQ(std::get<0>(winner), 1);
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
        co_await sleep(std::chrono::milliseconds{1}, loop);
        co_await cancel();
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        slow_started += 1;
        co_await sleep(std::chrono::milliseconds{5}, loop);
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

TEST_CASE(any_catch_cancel_promotes_aggregate_cancellation) {
    event_loop loop;
    int slow_done = 0;

    auto canceler = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        co_await cancel();
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{5}, loop);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<> {
        auto result = co_await when_any(slow(), canceler().catch_cancel());
        EXPECT_TRUE(result.is_cancelled());
    };

    auto task = combined();
    loop.schedule(task);
    loop.run();

    EXPECT_TRUE(task->is_finished());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(all_cancel_others) {
    event_loop loop;
    int cancel_started = 0;
    int slow_started = 0;
    int slow_done = 0;

    auto canceler = [&]() -> task<int> {
        cancel_started += 1;
        co_await sleep(std::chrono::milliseconds{1}, loop);
        co_await cancel();
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        slow_started += 1;
        co_await sleep(std::chrono::milliseconds{5}, loop);
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

TEST_CASE(all_catch_cancel_promotes_aggregate_cancellation) {
    event_loop loop;
    int slow_done = 0;

    auto canceler = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        co_await cancel();
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{5}, loop);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<> {
        auto result = co_await when_all(slow(), canceler().catch_cancel());
        EXPECT_TRUE(result.is_cancelled());
    };

    auto task = combined();
    loop.schedule(task);
    loop.run();

    EXPECT_TRUE(task->is_finished());
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

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(slow(), fast());
    };

    auto [winner] = run(combined());
    EXPECT_TRUE(winner.has_value());
    EXPECT_EQ(winner->index(), 1U);
    EXPECT_EQ(std::get<1>(*winner), 1);
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
        co_await sleep(std::chrono::milliseconds{10}, loop);
        finished += 1;
        co_return 1;
    };

    auto slow2 = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{10}, loop);
        finished += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<int> {
        auto [a, b] = co_await when_all(slow1(), slow2());
        co_return a + b;
    };

    auto guarded = with_token(combined(), source.token());

    auto canceler = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
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
        co_await sleep(std::chrono::milliseconds{10}, loop);
        finished += 1;
        co_return 1;
    };

    auto slow2 = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{10}, loop);
        finished += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(slow1(), slow2());
    };

    auto guarded = with_token(combined(), source.token());

    auto canceler = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
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
        co_await sleep(std::chrono::milliseconds{1}, loop);
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

    auto combined = [&]() -> task<std::variant<int>> {
        co_return co_await when_any(a());
    };

    auto [winner] = run(combined());
    EXPECT_TRUE(winner.has_value());
    EXPECT_EQ(winner->index(), 0U);
    EXPECT_EQ(std::get<0>(*winner), 99);
}

TEST_CASE(when_any_second_wins) {
    event_loop loop;

    auto slow = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{10}, loop);
        co_return 1;
    };

    auto fast = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        co_return 2;
    };

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(slow(), fast());
    };

    auto task = combined();
    loop.schedule(task);
    loop.run();

    auto winner = task.result();
    EXPECT_EQ(winner.index(), 1U);
    EXPECT_EQ(std::get<1>(winner), 2);
}

#if EVENTIDE_ENABLE_EXCEPTIONS
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
        co_await sleep(std::chrono::milliseconds{1}, loop);
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

TEST_CASE(when_all_accepts_sync_awaiters) {
    event_loop loop;
    semaphore sem{0};
    int resumed = 0;

    auto releaser = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        sem.release(2);
    };

    auto combined = [&]() -> task<int> {
        co_await when_all(sem.acquire(), sem.acquire());
        resumed += 1;
        co_return 7;
    };

    auto task = combined();
    auto release_task = releaser();
    loop.schedule(task);
    loop.schedule(release_task);
    loop.run();

    EXPECT_TRUE(task->is_finished());
    EXPECT_EQ(task.result(), 7);
    EXPECT_EQ(resumed, 1);
}

TEST_CASE(when_any_accepts_sync_awaiters) {
    event_loop loop;
    semaphore slow{0};
    semaphore fast{0};

    auto releaser = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        fast.release();
        co_await sleep(std::chrono::milliseconds{1}, loop);
        slow.release();
    };

    auto combined = [&]() -> task<std::variant<std::nullopt_t, std::nullopt_t>> {
        co_return co_await when_any(slow.acquire(), fast.acquire());
    };

    auto task = combined();
    auto release_task = releaser();
    loop.schedule(task);
    loop.schedule(release_task);
    loop.run();

    EXPECT_TRUE(task->is_finished());
    auto winner = task.result();
    EXPECT_EQ(winner.index(), 1U);
}

TEST_CASE(when_all_range_values) {
    small_vector<task<int>> tasks;
    tasks.emplace_back(ready_int(3));
    tasks.emplace_back(ready_int(4));

    auto combined = [&]() -> task<int> {
        auto values = co_await when_all(std::move(tasks));
        EXPECT_EQ(values.size(), 2U);
        co_return values[0] + values[1];
    };

    auto [sum] = run(combined());
    EXPECT_EQ(sum, 7);
}

TEST_CASE(when_all_range_empty) {
    small_vector<task<int>> tasks;

    auto combined = [&]() -> task<std::size_t> {
        auto values = co_await when_all(std::move(tasks));
        co_return values.size();
    };

    auto [size] = run(combined());
    EXPECT_EQ(size, 0U);
}

TEST_CASE(when_all_range_void_values) {
    small_vector<task<>> tasks;
    tasks.emplace_back(ready_void());
    tasks.emplace_back(ready_void());

    auto combined = [&]() -> task<std::size_t> {
        auto values = co_await when_all(std::move(tasks));
        EXPECT_EQ(values.size(), 2U);
        co_return values.size();
    };

    auto [size] = run(combined());
    EXPECT_EQ(size, 2U);
}

TEST_CASE(when_all_range_accepts_sync_awaiters) {
    event_loop loop;
    semaphore sem{0};
    small_vector<semaphore::acquire_awaiter> waits;
    waits.emplace_back(sem.acquire());
    waits.emplace_back(sem.acquire());

    auto releaser = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        sem.release(2);
    };

    auto combined = [&]() -> task<std::size_t> {
        auto values = co_await when_all(std::move(waits));
        EXPECT_EQ(values.size(), 2U);
        co_return values.size();
    };

    auto task = combined();
    auto release_task = releaser();
    loop.schedule(task);
    loop.schedule(release_task);
    loop.run();

    EXPECT_EQ(task.result(), 2U);
}

TEST_CASE(when_any_range_values) {
    event_loop loop;
    small_vector<task<int>> tasks;
    tasks.emplace_back(delayed_int(loop, 10, 1));
    tasks.emplace_back(delayed_int(loop, 1, 2));

    auto combined = [&]() -> task<std::pair<std::size_t, int>> {
        co_return co_await when_any(std::move(tasks));
    };

    auto task = combined();
    loop.schedule(task);
    loop.run();

    auto winner = task.result();
    EXPECT_EQ(winner.first, 1U);
    EXPECT_EQ(winner.second, 2);
}

TEST_CASE(when_any_range_void_values) {
    event_loop loop;
    semaphore slow{0};
    semaphore fast{0};
    small_vector<semaphore::acquire_awaiter> waits;
    waits.emplace_back(slow.acquire());
    waits.emplace_back(fast.acquire());

    auto releaser = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        fast.release();
        co_await sleep(std::chrono::milliseconds{1}, loop);
        slow.release();
    };

    auto combined = [&]() -> task<std::pair<std::size_t, std::nullopt_t>> {
        co_return co_await when_any(std::move(waits));
    };

    auto task = combined();
    auto release_task = releaser();
    loop.schedule(task);
    loop.schedule(release_task);
    loop.run();

    auto winner = task.result();
    EXPECT_EQ(winner.first, 1U);
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

TEST_CASE(scope_accepts_sync_awaiters) {
    event_loop loop;
    semaphore sem{0};
    int count = 0;

    auto releaser = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        sem.release();
        count += 1;
    };

    auto driver = [&]() -> task<> {
        async_scope scope;
        scope.spawn(sem.acquire());
        scope.spawn(releaser());
        co_await scope;
        count += 10;
    };

    auto task = driver();
    loop.schedule(task);
    loop.run();

    EXPECT_TRUE(task->is_finished());
    EXPECT_EQ(count, 11);
}

TEST_CASE(scope_with_sleep) {
    event_loop loop;
    int count = 0;

    auto work = [&](int val, int ms) -> task<> {
        co_await sleep(std::chrono::milliseconds{ms}, loop);
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
        co_await sleep(std::chrono::milliseconds{1}, loop);
        co_await cancel();
    };

    auto slow = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{10}, loop);
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
        co_await sleep(std::chrono::milliseconds{ms}, loop);
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
        co_await sleep(std::chrono::milliseconds{1}, loop);
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
            co_await sleep(std::chrono::milliseconds{1}, loop);
            scope_count += 1;
        };
        for(int i = 0; i < 3; ++i) {
            scope.spawn(work());
        }
        co_await scope;
        co_return scope_count;
    };

    auto normal = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
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
            co_await sleep(std::chrono::milliseconds{1}, loop);
            co_return 1;
        };
        auto b = [&]() -> task<int> {
            co_await sleep(std::chrono::milliseconds{1}, loop);
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

// ============================================================================
// Structured concurrency: exception and error propagation in aggregates
// ============================================================================

namespace {

task<int, error> return_error(error err) {
    co_return outcome_error(err);
}

task<int, error> return_value(int val) {
    co_return val;
}

task<int, error> delayed_return_error(event_loop& loop, int ms, error err) {
    co_await sleep(std::chrono::milliseconds{ms}, loop);
    co_return outcome_error(err);
}

task<int, error> delayed_return_value(event_loop& loop, int ms, int val) {
    co_await sleep(std::chrono::milliseconds{ms}, loop);
    co_return val;
}

}  // namespace

#if EVENTIDE_ENABLE_EXCEPTIONS

TEST_SUITE(structured_concurrency_exceptions) {

TEST_CASE(exception_in_when_all_cancels_siblings) {
    event_loop loop;
    int slow_done = 0;

    auto thrower = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        throw std::runtime_error("boom");
        co_return 0;
    };

    auto slow = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{50}, loop);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<int> {
        auto [a, b] = co_await when_all(thrower(), slow());
        co_return a + b;
    };

    auto t = combined();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_failed());
    EXPECT_THROWS(t.result());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(exception_in_when_all_immediate) {
    auto thrower = []() -> task<int> {
        throw std::runtime_error("immediate boom");
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

TEST_CASE(exception_in_when_any_cancels_siblings) {
    event_loop loop;
    int slow_done = 0;

    auto thrower = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        throw std::runtime_error("boom");
        co_return 0;
    };

    auto slow = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{50}, loop);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<> {
        co_await when_any(thrower(), slow());
    };

    auto t = combined();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_failed());
    EXPECT_THROWS(t.result());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(exception_in_scope_cancels_siblings) {
    event_loop loop;
    int slow_done = 0;

    auto thrower = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        throw std::runtime_error("scope boom");
    };

    auto slow = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{50}, loop);
        slow_done += 1;
    };

    auto driver = [&]() -> task<> {
        async_scope scope;
        scope.spawn(thrower());
        scope.spawn(slow());
        co_await scope;
    };

    auto t = driver();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_failed());
    EXPECT_THROWS(t.result());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(exception_in_when_all_range) {
    event_loop loop;
    int slow_done = 0;

    auto thrower = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        throw std::runtime_error("range boom");
        co_return 0;
    };

    auto slow = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{50}, loop);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<int> {
        small_vector<task<int>> tasks;
        tasks.emplace_back(thrower());
        tasks.emplace_back(slow());
        auto results = co_await when_all(std::move(tasks));
        co_return results[0] + results[1];
    };

    auto t = combined();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_failed());
    EXPECT_THROWS(t.result());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(exception_in_when_any_range) {
    event_loop loop;
    int slow_done = 0;

    auto thrower = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        throw std::runtime_error("range any boom");
        co_return 0;
    };

    auto slow = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{50}, loop);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<> {
        small_vector<task<int>> tasks;
        tasks.emplace_back(thrower());
        tasks.emplace_back(slow());
        co_await when_any(std::move(tasks));
    };

    auto t = combined();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_failed());
    EXPECT_THROWS(t.result());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(exception_propagates_through_nested_when_all) {
    event_loop loop;

    auto thrower = [&]() -> task<int> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        throw std::runtime_error("deep boom");
        co_return 0;
    };

    auto inner = [&]() -> task<int> {
        auto [a, b] = co_await when_all(thrower(), delayed_int(loop, 50, 1));
        co_return a + b;
    };

    auto outer = [&]() -> task<int> {
        auto [a, b] = co_await when_all(inner(), delayed_int(loop, 50, 2));
        co_return a + b;
    };

    auto t = outer();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_failed());
    EXPECT_THROWS(t.result());
}

TEST_CASE(exception_caught_by_parent_does_not_propagate) {
    event_loop loop;

    auto thrower = [&]() -> task<int> {
        throw std::runtime_error("caught boom");
        co_return 0;
    };

    auto catcher = [&]() -> task<int> {
        try {
            co_return co_await thrower();
        } catch(const std::runtime_error&) {
            co_return -1;
        }
    };

    auto combined = [&]() -> task<int> {
        auto [a, b] = co_await when_all(catcher(), delayed_int(loop, 1, 42));
        co_return a + b;
    };

    auto t = combined();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(t.result(), 41);
}

TEST_CASE(exception_in_direct_co_await_rethrows) {
    auto thrower = []() -> task<int> {
        throw std::runtime_error("direct boom");
        co_return 0;
    };

    auto parent = [&]() -> task<int> {
        co_return co_await thrower();
    };

    EXPECT_THROWS(run(parent()));
}

};  // TEST_SUITE(structured_concurrency_exceptions)

#endif  // EVENTIDE_ENABLE_EXCEPTIONS

TEST_SUITE(structured_concurrency_errors) {

TEST_CASE(propagating_error_in_when_all_cancels_siblings) {
    event_loop loop;
    int slow_done = 0;

    auto failing = [&]() -> task<int, error> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        co_return outcome_error(error::connection_refused);
    };

    auto slow = [&]() -> task<int, error> {
        co_await sleep(std::chrono::milliseconds{50}, loop);
        slow_done += 1;
        co_return 42;
    };

    auto combined = [&]() -> task<> {
        auto res = co_await when_all(failing(), slow());
        EXPECT_TRUE(res.has_error());
        EXPECT_EQ(res.error(), error::connection_refused);
    };

    auto t = combined();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(propagating_error_immediate_when_all) {
    auto failing = []() -> task<int, error> {
        co_return outcome_error(error::connection_refused);
    };

    auto normal = []() -> task<int, error> {
        co_return 42;
    };

    auto combined = [&]() -> task<> {
        auto res = co_await when_all(failing(), normal());
        EXPECT_TRUE(res.has_error());
        EXPECT_EQ(res.error(), error::connection_refused);
    };

    run(combined());
}

TEST_CASE(propagating_error_in_direct_co_await_returns_error) {
    auto failing = []() -> task<int, error> {
        co_return outcome_error(error::connection_refused);
    };

    auto parent = [&]() -> task<int, error> {
        co_return co_await failing();
    };

    auto [res] = run(parent());
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(res->has_error());
    EXPECT_EQ(res->error(), error::connection_refused);
}

TEST_CASE(operation_aborted_error_now_cancels_siblings) {
    event_loop loop;
    int slow_done = 0;

    auto aborting = [&]() -> task<int, error> {
        co_return outcome_error(error::operation_aborted);
    };

    auto slow = [&]() -> task<int, error> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        slow_done += 1;
        co_return 42;
    };

    auto combined = [&]() -> task<> {
        auto res = co_await when_all(aborting(), slow());
        EXPECT_TRUE(res.has_error());
        EXPECT_EQ(res.error(), error::operation_aborted);
    };

    auto t = combined();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(eof_error_now_cancels_siblings) {
    event_loop loop;
    int slow_done = 0;

    auto eof_task = [&]() -> task<int, error> {
        co_return outcome_error(error::end_of_file);
    };

    auto slow = [&]() -> task<int, error> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        slow_done += 1;
        co_return 99;
    };

    auto combined = [&]() -> task<> {
        auto res = co_await when_all(eof_task(), slow());
        EXPECT_TRUE(res.has_error());
        EXPECT_EQ(res.error(), error::end_of_file);
    };

    auto t = combined();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(propagating_error_in_when_any_returns_outer_error) {
    event_loop loop;
    int slow_done = 0;

    auto failing = [&]() -> task<int, error> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        co_return outcome_error(error::connection_refused);
    };

    auto slow = [&]() -> task<int, error> {
        co_await sleep(std::chrono::milliseconds{50}, loop);
        slow_done += 1;
        co_return 42;
    };

    auto combined = [&]() -> task<> {
        auto res = co_await when_any(failing(), slow());
        EXPECT_TRUE(res.has_error());
        EXPECT_EQ(res.error(), error::connection_refused);
    };

    auto t = combined();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(direct_co_await_error_inside_scope_child_does_not_escape) {
    event_loop loop;
    int slow_done = 0;

    auto failing = [&]() -> task<> {
        auto inner = [&]() -> task<int, error> {
            co_await sleep(std::chrono::milliseconds{1}, loop);
            co_return outcome_error(error::connection_refused);
        };
        auto res = co_await inner();
        (void)res;
    };

    auto slow = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{5}, loop);
        slow_done += 1;
    };

    auto driver = [&]() -> task<> {
        async_scope scope;
        scope.spawn(failing());
        scope.spawn(slow());
        co_await scope;
    };

    auto t = driver();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(slow_done, 1);
}

TEST_CASE(async_scope_returns_structured_error) {
    event_loop loop;
    int slow_done = 0;

    auto failing = [&]() -> task<int, error> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        co_return outcome_error(error::connection_refused);
    };

    auto slow = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{50}, loop);
        slow_done += 1;
    };

    auto driver = [&]() -> task<> {
        async_scope<error> scope;
        scope.spawn(failing());
        scope.spawn(slow());
        auto res = co_await scope;
        EXPECT_TRUE(res.has_error());
        EXPECT_EQ(res.error(), error::connection_refused);
    };

    auto t = driver();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(async_scope_mixed_error_types) {
    event_loop loop;
    int slow_done = 0;

    auto failing = [&]() -> task<int, custom_error> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        co_return outcome_error(custom_error{7});
    };

    auto slow = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{50}, loop);
        slow_done += 1;
    };

    auto driver = [&]() -> task<> {
        async_scope<error, custom_error> scope;
        scope.spawn(failing());
        scope.spawn(slow());
        auto res = co_await scope;
        EXPECT_TRUE(res.has_error());
        EXPECT_EQ(std::get<custom_error>(res.error()), custom_error{7});
    };

    auto t = driver();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(propagating_error_in_when_all_range) {
    event_loop loop;
    int slow_done = 0;

    auto combined = [&]() -> task<> {
        small_vector<task<int, error>> tasks;
        tasks.emplace_back(delayed_return_error(loop, 1, error::connection_refused));
        tasks.emplace_back(delayed_return_value(loop, 50, 42));
        auto res = co_await when_all(std::move(tasks));
        EXPECT_TRUE(res.has_error());
        EXPECT_EQ(res.error(), error::connection_refused);
    };

    auto t = combined();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_finished());
}

TEST_CASE(propagating_error_in_when_any_range) {
    event_loop loop;
    int slow_done = 0;

    auto combined = [&]() -> task<> {
        small_vector<task<int, error>> tasks;
        tasks.emplace_back(delayed_return_error(loop, 1, error::connection_refused));
        tasks.emplace_back(delayed_return_value(loop, 50, 42));
        auto res = co_await when_any(std::move(tasks));
        EXPECT_TRUE(res.has_error());
        EXPECT_EQ(res.error(), error::connection_refused);
    };

    auto t = combined();
    loop.schedule(t);
    loop.run();

    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(successful_task_in_when_all_no_false_failure) {
    auto a = []() -> task<int, error> {
        co_return 1;
    };

    auto b = []() -> task<int, error> {
        co_return 2;
    };

    auto combined = [&]() -> task<> {
        auto res = co_await when_all(a(), b());
        EXPECT_TRUE(res.has_value());
        auto [ra, rb] = *res;
        EXPECT_EQ(ra, 1);
        EXPECT_EQ(rb, 2);
    };

    run(combined());
}

TEST_CASE(mixed_error_and_void_in_when_all) {
    auto failing = []() -> task<int, error> {
        co_return outcome_error(error::connection_refused);
    };

    auto void_task = []() -> task<> {
        co_return;
    };

    auto combined = [&]() -> task<> {
        auto res = co_await when_all(failing(), void_task());
        EXPECT_TRUE(res.has_error());
        EXPECT_EQ(res.error(), error::connection_refused);
    };

    run(combined());
}

TEST_CASE(nested_manual_error_propagation) {
    event_loop loop;

    auto failing = [&]() -> task<int, error> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        co_return outcome_error(error::connection_refused);
    };

    auto parent = [&]() -> task<int, error> {
        auto res = co_await when_all(failing(), delayed_return_value(loop, 10, 42));
        if(!res) {
            co_return outcome_error(std::move(res).error());
        }
        auto [a, b] = *res;
        co_return a + b;
    };

    auto t = parent();
    loop.schedule(t);
    loop.run();

    auto res = t.result();
    EXPECT_TRUE(res.has_error());
    EXPECT_EQ(res.error(), error::connection_refused);
}

TEST_CASE(with_token_returns_structured_error_explicitly) {
    event_loop loop;
    cancellation_source source;

    auto failing = [&]() -> task<int, error> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        co_return outcome_error(error::connection_refused);
    };

    auto wrapped = with_token(failing(), source.token());

    loop.schedule(wrapped);
    loop.run();

    auto res = wrapped.result();
    EXPECT_TRUE(res.has_error());
    EXPECT_EQ(res.error(), error::connection_refused);
}

TEST_CASE(with_token_cancels_error_task) {
    event_loop loop;
    cancellation_source source;

    auto slow = [&]() -> task<int, error> {
        co_await sleep(std::chrono::milliseconds{50}, loop);
        co_return 42;
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        source.cancel();
    };

    auto wrapped = with_token(slow(), source.token());
    auto cancel_task = canceler();

    loop.schedule(wrapped);
    loop.schedule(cancel_task);
    loop.run();

    EXPECT_TRUE(wrapped.result().is_cancelled());
}

};  // TEST_SUITE(structured_concurrency_errors)

}  // namespace

}  // namespace eventide
