// ZEST_SUITE(when_any): basic value passing for when_any — first/second wins,
// single task, sleeping children, sync awaiters, and range overloads. The
// result-type static_asserts live in all_values.cpp; cancellation in cancel.cpp;
// errors in errors.cpp.
#include "../../loop_fixture.h"
#include "../../support.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

ZEST_SUITE(when_any) {

ZEST_CASE(first_wins) {
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
    EXPECT(winner);
    EXPECT(winner->index() == 0U);
    EXPECT(std::get<0>(*winner) == 10);
    EXPECT(a_count == 1);
    EXPECT(b_count == 0);
}

ZEST_CASE(single_task) {
    auto a = []() -> task<int> {
        co_return 99;
    };

    auto combined = [&]() -> task<std::variant<int>> {
        co_return co_await when_any(a());
    };

    auto [winner] = run(combined());
    EXPECT(winner);
    EXPECT(winner->index() == 0U);
    EXPECT(std::get<0>(*winner) == 99);
}

ZEST_CASE(second_wins) {
    auto slow = [&]() -> task<int> {
        co_await sleep(10);
        co_return 1;
    };

    auto fast = [&]() -> task<int> {
        co_await sleep(1);
        co_return 2;
    };

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(slow(), fast());
    };

    auto [winner] = run(combined());
    EXPECT(winner);
    EXPECT(winner->index() == 1U);
    EXPECT(std::get<1>(*winner) == 2);
}

ZEST_CASE(with_sleep) {
    int fast_done = 0;
    int slow_done = 0;

    auto fast = [&]() -> task<int> {
        co_await sleep(1);
        fast_done += 1;
        co_return 1;
    };

    auto slow = [&]() -> task<int> {
        co_await sleep(10);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(fast(), slow());
    };

    auto [winner] = run(combined());
    EXPECT(winner);
    EXPECT(winner->index() == 0U);
    EXPECT(std::get<0>(*winner) == 1);
    EXPECT(fast_done == 1);
    EXPECT(slow_done == 0);
}

ZEST_CASE(accepts_sync_awaiters) {
    semaphore slow{0};
    semaphore fast{0};

    auto releaser = [&]() -> task<> {
        co_await sleep(1);
        fast.release();
        co_await sleep(1);
        slow.release();
    };

    auto combined = [&]() -> task<std::variant<std::nullopt_t, std::nullopt_t>> {
        co_return co_await when_any(slow.acquire(), fast.acquire());
    };

    auto task = combined();
    auto release_task = releaser();
    run(task, release_task);

    EXPECT(task->is_finished());
    auto winner = task.result();
    EXPECT(winner.index() == 1U);
}

ZEST_CASE(range_values) {
    small_vector<task<int>> tasks;
    tasks.emplace_back(delayed_int(10, 1));
    tasks.emplace_back(delayed_int(1, 2));

    auto combined = [&]() -> task<std::pair<std::size_t, int>> {
        co_return co_await when_any(std::move(tasks));
    };

    auto [winner] = run(combined());
    EXPECT(winner);
    EXPECT(winner->first == 1U);
    EXPECT(winner->second == 2);
}

ZEST_CASE(range_void) {
    semaphore slow{0};
    semaphore fast{0};
    small_vector<semaphore::acquire_awaiter> waits;
    waits.emplace_back(slow.acquire());
    waits.emplace_back(fast.acquire());

    auto releaser = [&]() -> task<> {
        co_await sleep(1);
        fast.release();
        co_await sleep(1);
        slow.release();
    };

    auto combined = [&]() -> task<std::pair<std::size_t, std::nullopt_t>> {
        co_return co_await when_any(std::move(waits));
    };

    auto task = combined();
    auto release_task = releaser();
    run(task, release_task);

    auto winner = task.result();
    EXPECT(winner.first == 1U);
}

ZEST_CASE(range_single_element) {
    auto combined = []() -> task<std::pair<std::size_t, int>> {
        small_vector<task<int>> tasks;
        tasks.emplace_back(ready_int(42));
        co_return co_await when_any(std::move(tasks));
    };

    auto [res] = run(combined());
    ASSERT(res);
    EXPECT(res->first == 0U);
    EXPECT(res->second == 42);
}

};  // ZEST_SUITE(when_any)

}  // namespace kota
