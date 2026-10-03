#include <coroutine>
#include <cstddef>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

struct CustomError {
    int code = 0;
};

template <typename... Tasks>
using all_result_t = decltype(std::declval<when_all<Tasks...>&>().await_resume());

template <typename Range>
using all_range_result_t =
    decltype(std::declval<decltype(when_all(std::declval<Range>()))&>().await_resume());

task<int> value(int v) {
    co_return v;
}

/// An awaiter that never suspends and gives `value`.
struct Ready {
    int value = 0;

    bool await_ready() const noexcept {
        return true;
    }

    void await_suspend(std::coroutine_handle<>) const noexcept {}

    int await_resume() const noexcept {
        return value;
    }
};

/// An awaiter that suspends until a later turn of the loop, then gives
/// `value`.
struct Yielded {
    yield_awaiter turn;
    int value = 0;

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> waiting) noexcept {
        return turn.await_suspend(waiting);
    }

    int await_resume() const noexcept {
        return value;
    }
};

ZEST_SUITE(async_runtime_when_all, zest::LoopFixture) {

ZEST_CASE(result_type_follows_the_children_channels) {
    EXPECT(zest::type_eq<all_result_t<task<int>, task<int>>, std::tuple<int, int>>());
    EXPECT(zest::type_eq<all_result_t<task<int>, task<>>, std::tuple<int, std::nullopt_t>>());
    EXPECT(zest::type_eq<all_result_t<task<int, error>, task<int>>,
                         outcome<std::tuple<int, int>, error, void>>());
    EXPECT(zest::type_eq<all_result_t<task<int, error>, task<>>,
                         outcome<std::tuple<int, std::nullopt_t>, error, void>>());
    EXPECT(zest::type_eq<all_result_t<task<int, error>, task<int, error>>,
                         outcome<std::tuple<int, int>, error, void>>());
    EXPECT(zest::type_eq<all_result_t<task<int, error>, task<int, CustomError>>,
                         outcome<std::tuple<int, int>, std::variant<error, CustomError>, void>>());
    EXPECT(zest::type_eq<all_result_t<task<int, void, cancellation>, task<int>>,
                         outcome<std::tuple<int, int>, void, cancellation>>());
    EXPECT(zest::type_eq<all_result_t<task<int, error, cancellation>, task<>>,
                         outcome<std::tuple<int, std::nullopt_t>, error, cancellation>>());
    EXPECT(zest::type_eq<all_range_result_t<std::vector<task<int>>>, small_vector<int>>());
    EXPECT(zest::type_eq<all_range_result_t<std::vector<task<int, error>>>,
                         outcome<small_vector<int>, error, void>>());
}

ZEST_CASE(values_come_back_in_argument_order) {
    auto three = []() -> task<std::tuple<int, int, int>> {
        co_return co_await when_all(value(1), value(2), value(3));
    };
    auto one = []() -> task<std::tuple<int>> {
        co_return co_await when_all(value(42));
    };

    auto [three_values, one_value] = run(three(), one());
    ASSERT(three_values.has_value());
    EXPECT(*three_values == std::tuple{1, 2, 3});
    ASSERT(one_value.has_value());
    EXPECT(*one_value == std::tuple{42});
}

ZEST_CASE(void_children_give_nullopt) {
    int ran = 0;
    auto child = [&]() -> task<> {
        ran += 1;
        co_return;
    };
    auto combined = [&]() -> task<std::tuple<std::nullopt_t, std::nullopt_t>> {
        co_return co_await when_all(child(), child());
    };

    auto [result] = run(combined());
    EXPECT(result.has_value());
    EXPECT(ran == 2);
}

ZEST_CASE(waits_for_the_last_child) {
    event gates[2];
    std::vector<int> finished;
    bool combined_done = false;
    auto child = [&](int id) -> task<int> {
        co_await gates[id].wait();
        finished.push_back(id);
        co_return id * 10;
    };
    auto combined = [&]() -> task<std::tuple<int, int>> {
        auto values = co_await when_all(child(0), child(1));
        combined_done = true;
        co_return values;
    };
    auto driver = [&]() -> task<bool> {
        gates[1].set();
        co_await yield();
        bool done_after_one = combined_done;
        gates[0].set();
        co_return done_after_one;
    };

    auto [result, done_after_one] = run(combined(), driver());
    ASSERT(result.has_value());
    EXPECT(*result == std::tuple{0, 10});
    EXPECT(finished == std::vector{1, 0});
    ASSERT(done_after_one.has_value());
    EXPECT(!*done_after_one);
}

ZEST_CASE(accepts_awaiters_that_are_not_tasks) {
    semaphore sem;
    auto combined = [&]() -> task<std::tuple<std::nullopt_t, std::nullopt_t>> {
        co_return co_await when_all(sem.acquire(), sem.acquire());
    };
    auto releaser = [&]() -> task<> {
        sem.release(2);
        co_return;
    };

    auto [result, driver] = run(combined(), releaser());
    EXPECT(result.has_value());
    EXPECT(!sem.try_acquire());
}

ZEST_CASE(accepts_awaiters_that_return_values) {
    auto combined = []() -> task<std::tuple<int, int>> {
        co_return co_await when_all(Ready{.value = 1}, Ready{.value = 2});
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(*result == std::tuple{1, 2});
}

ZEST_CASE(accepts_awaiters_that_suspend_and_return_values) {
    auto combined = [&]() -> task<std::tuple<int, int>> {
        co_return co_await when_all(Yielded{.turn = yield(loop), .value = 1},
                                    Yielded{.turn = yield(loop), .value = 2});
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(*result == std::tuple{1, 2});
}

ZEST_CASE(empty_when_all_completes_at_once) {
    auto combined = []() -> task<std::tuple<>> {
        co_return co_await when_all();
    };

    auto [result] = run(combined());
    EXPECT(result.has_value());
}

ZEST_CASE(range_values_come_back_in_order) {
    auto combined = []() -> task<std::vector<int>> {
        std::vector<task<int>> tasks;
        tasks.push_back(value(3));
        tasks.push_back(value(4));
        auto values = co_await when_all(std::move(tasks));
        co_return std::vector<int>(values.begin(), values.end());
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(*result == std::vector{3, 4});
}

ZEST_CASE(empty_range_completes_at_once) {
    auto combined = []() -> task<std::size_t> {
        small_vector<task<int>> tasks;
        auto values = co_await when_all(std::move(tasks));
        co_return values.size();
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(*result == 0U);
}

ZEST_CASE(range_of_void_tasks_gives_nullopt) {
    int ran = 0;
    auto child = [&]() -> task<> {
        ran += 1;
        co_return;
    };
    auto combined = [&]() -> task<std::size_t> {
        small_vector<task<>> tasks;
        tasks.emplace_back(child());
        tasks.emplace_back(child());
        auto values = co_await when_all(std::move(tasks));
        co_return values.size();
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(*result == 2U);
    EXPECT(ran == 2);
}

ZEST_CASE(range_of_awaiters_that_are_not_tasks) {
    semaphore sem;
    auto combined = [&]() -> task<std::size_t> {
        small_vector<semaphore::acquire_awaiter> waits;
        waits.emplace_back(sem.acquire());
        waits.emplace_back(sem.acquire());
        auto values = co_await when_all(std::move(waits));
        co_return values.size();
    };
    auto releaser = [&]() -> task<> {
        sem.release(2);
        co_return;
    };

    auto [result, driver] = run(combined(), releaser());
    ASSERT(result.has_value());
    EXPECT(*result == 2U);
    EXPECT(!sem.try_acquire());
}

};  // ZEST_SUITE(async_runtime_when_all)

}  // namespace

}  // namespace kota
