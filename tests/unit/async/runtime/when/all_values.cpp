// ZEST_SUITE(when_all): basic value passing for when_all — tuples, void tasks,
// single/three tasks, sleeping children, sync awaiters, and range overloads.
// Also hosts the type-level result-type static_asserts for both when_all and
// when_any (result computation, error/cancel dedup, void → nullopt_t).
// Cancellation lives in cancel.cpp; errors in errors.cpp; when_any in any_values.cpp.
#include <utility>

#include "../../loop_fixture.h"
#include "../../support.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

// ============================================================================
// Type-level: result type aliases
// ============================================================================

template <typename... Tasks>
using when_all_result = decltype(std::declval<when_all<Tasks...>>().await_resume());

template <typename... Tasks>
using when_any_result = decltype(std::declval<when_any<Tasks...>>().await_resume());

// ============================================================================
// Type-level: concepts and basic constraints
// ============================================================================

static_assert(detail::async_range<small_vector<task<int>>>);
static_assert(!std::constructible_from<when_any<>>);

// ============================================================================
// Type-level: result type computation
// ============================================================================

// --- Case 1: all errors void, all cancels void -> bare tuple/variant ---
static_assert(std::same_as<when_all_result<task<int>, task<int>>, std::tuple<int, int>>);
static_assert(std::same_as<when_any_result<task<int>, task<int>>, std::variant<int, int>>);

// --- Case 2: all errors void, some cancels non-void -> outcome<..., void, cancellation> ---
static_assert(std::same_as<when_all_result<task<int, void, cancellation>, task<int>>,
                           outcome<std::tuple<int, int>, void, cancellation>>);
static_assert(std::same_as<when_any_result<task<int, void, cancellation>, task<int>>,
                           outcome<std::variant<int, int>, void, cancellation>>);

// --- Case 3: some errors non-void, all cancels void -> outcome<..., error, void> ---
static_assert(std::same_as<when_all_result<task<int, error>, task<int>>,
                           outcome<std::tuple<int, int>, error, void>>);
static_assert(std::same_as<when_any_result<task<int, error>, task<int>>,
                           outcome<std::variant<int, int>, error, void>>);

// --- Case 4: both errors and cancels non-void -> outcome<..., error, cancellation> ---
static_assert(std::same_as<when_all_result<task<int, error, cancellation>, task<int>>,
                           outcome<std::tuple<int, int>, error, cancellation>>);
static_assert(std::same_as<when_any_result<task<int, error, cancellation>, task<int>>,
                           outcome<std::variant<int, int>, error, cancellation>>);

// --- Case 5: same error type deduplication ---
static_assert(std::same_as<when_all_result<task<int, error>, task<int, error>>,
                           outcome<std::tuple<int, int>, error, void>>);
static_assert(std::same_as<when_any_result<task<int, error>, task<int, error>>,
                           outcome<std::variant<int, int>, error, void>>);

// --- Case 6: mixed error types -> variant<error, custom_error> ---
static_assert(std::same_as<when_all_result<task<int, error>, task<int, custom_error>>,
                           outcome<std::tuple<int, int>, std::variant<error, custom_error>, void>>);
static_assert(
    std::same_as<when_any_result<task<int, error>, task<int, custom_error>>,
                 outcome<std::variant<int, int>, std::variant<error, custom_error>, void>>);

// --- Case 7: void value type -> nullopt_t in tuple/variant ---
static_assert(std::same_as<when_all_result<task<int>, task<>>, std::tuple<int, std::nullopt_t>>);
static_assert(std::same_as<when_any_result<task<int>, task<>>, std::variant<int, std::nullopt_t>>);
static_assert(std::same_as<when_all_result<task<int, error>, task<>>,
                           outcome<std::tuple<int, std::nullopt_t>, error, void>>);
static_assert(std::same_as<when_any_result<task<int, error>, task<>>,
                           outcome<std::variant<int, std::nullopt_t>, error, void>>);

// --- Case 8: task_group type checks ---
static_assert(group_spawnable<task_group<error>, task<int, error>>);
static_assert(!group_spawnable<task_group<>, task<int, error>>);
static_assert(group_spawnable<task_group<error>, task<>>);
static_assert(group_spawnable<task_group<error, custom_error>, task<int, custom_error>>);
static_assert(!group_spawnable<task_group<error>, task<int, custom_error>>);

}  // namespace

// ============================================================================
// ZEST_SUITE: when_all — basic value passing
// ============================================================================

ZEST_SUITE(when_all) {

ZEST_CASE(values) {
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
    EXPECT(res == 3);
}

ZEST_CASE(void_tasks) {
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
    EXPECT(count == 11);
}

ZEST_CASE(single_task) {
    auto a = []() -> task<int> {
        co_return 42;
    };

    auto combined = [&]() -> task<int> {
        auto [x] = co_await when_all(a());
        co_return x;
    };

    auto [res] = run(combined());
    EXPECT(res == 42);
}

ZEST_CASE(three_tasks) {
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
    EXPECT(res == 6);
}

ZEST_CASE(with_sleep) {
    int slow_done = 0;
    int fast_done = 0;

    auto slow = [&]() -> task<int> {
        co_await sleep(5);
        slow_done += 1;
        co_return 7;
    };

    auto fast = [&]() -> task<int> {
        co_await sleep(1);
        fast_done += 1;
        co_return 9;
    };

    auto combined = [&]() -> task<int> {
        auto [a, b] = co_await when_all(slow(), fast());
        co_return a + b;
    };

    auto [res] = run(combined());
    EXPECT(res == 16);
    EXPECT(slow_done == 1);
    EXPECT(fast_done == 1);
}

ZEST_CASE(accepts_sync_awaiters) {
    semaphore sem{0};
    int resumed = 0;

    auto releaser = [&]() -> task<> {
        co_await sleep(1);
        sem.release(2);
    };

    auto combined = [&]() -> task<int> {
        co_await when_all(sem.acquire(), sem.acquire());
        resumed += 1;
        co_return 7;
    };

    auto task = combined();
    auto release_task = releaser();
    run(task, release_task);

    EXPECT(task->is_finished());
    EXPECT(task.result() == 7);
    EXPECT(resumed == 1);
}

ZEST_CASE(range_values) {
    small_vector<task<int>> tasks;
    tasks.emplace_back(ready_int(3));
    tasks.emplace_back(ready_int(4));

    auto combined = [&]() -> task<int> {
        auto values = co_await when_all(std::move(tasks));
        EXPECT(values.size() == 2U);
        co_return values[0] + values[1];
    };

    auto [sum] = run(combined());
    EXPECT(sum == 7);
}

ZEST_CASE(range_empty) {
    small_vector<task<int>> tasks;

    auto combined = [&]() -> task<std::size_t> {
        auto values = co_await when_all(std::move(tasks));
        co_return values.size();
    };

    auto [size] = run(combined());
    EXPECT(size == 0U);
}

ZEST_CASE(range_void) {
    small_vector<task<>> tasks;
    tasks.emplace_back(ready_void());
    tasks.emplace_back(ready_void());

    auto combined = [&]() -> task<std::size_t> {
        auto values = co_await when_all(std::move(tasks));
        EXPECT(values.size() == 2U);
        co_return values.size();
    };

    auto [size] = run(combined());
    EXPECT(size == 2U);
}

ZEST_CASE(range_sync_awaiters) {
    semaphore sem{0};
    small_vector<semaphore::acquire_awaiter> waits;
    waits.emplace_back(sem.acquire());
    waits.emplace_back(sem.acquire());

    auto releaser = [&]() -> task<> {
        co_await sleep(1);
        sem.release(2);
    };

    auto combined = [&]() -> task<std::size_t> {
        auto values = co_await when_all(std::move(waits));
        EXPECT(values.size() == 2U);
        co_return values.size();
    };

    auto task = combined();
    auto release_task = releaser();
    run(task, release_task);

    EXPECT(task.result() == 2U);
}

};  // ZEST_SUITE(when_all)

}  // namespace kota
