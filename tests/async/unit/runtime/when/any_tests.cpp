#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/async/async.h"

namespace kota {

namespace {

struct CustomError {
    int code = 0;
};

template <typename... Tasks>
using any_result_t = decltype(std::declval<when_any<Tasks...>&>().await_resume());

template <typename Range>
using any_range_result_t =
    decltype(std::declval<decltype(when_any(std::declval<Range>()))&>().await_resume());

ZEST_SUITE(async_runtime_when_any, zest::LoopFixture) {

ZEST_CASE(result_type_follows_the_children_channels) {
    ZEXPECT(zest::type_eq<any_result_t<task<int>, task<int>>, std::variant<int, int>>());
    ZEXPECT(zest::type_eq<any_result_t<task<int>, task<>>, std::variant<int, std::nullopt_t>>());
    ZEXPECT(zest::type_eq<any_result_t<task<int, error>, task<int>>,
                          outcome<std::variant<int, int>, error, void>>());
    // One error type, however many children carry it.
    ZEXPECT(zest::type_eq<any_result_t<task<int, error>, task<int, error>>,
                          outcome<std::variant<int, int>, error, void>>());
    ZEXPECT(
        zest::type_eq<any_result_t<task<int, error>, task<int, CustomError>>,
                      outcome<std::variant<int, int>, std::variant<error, CustomError>, void>>());
    ZEXPECT(zest::type_eq<any_result_t<task<int, void, cancellation>, task<int>>,
                          outcome<std::variant<int, int>, void, cancellation>>());
    ZEXPECT(zest::type_eq<any_result_t<task<int, error, cancellation>, task<>>,
                          outcome<std::variant<int, std::nullopt_t>, error, cancellation>>());
    ZEXPECT(
        zest::type_eq<any_range_result_t<std::vector<task<int>>>, std::pair<std::size_t, int>>());
    ZEXPECT(zest::type_eq<any_range_result_t<small_vector<semaphore::acquire_awaiter>>,
                          std::pair<std::size_t, std::nullopt_t>>());
    ZSTATIC_EXPECT(!std::constructible_from<when_any<>>);
}

ZEST_CASE(first_child_to_finish_wins) {
    event gates[2];
    int finished = 0;
    auto child = [&](int id) -> task<int> {
        co_await gates[id].wait();
        finished += 1;
        co_return id * 10;
    };
    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(child(0), child(1));
    };
    auto driver = [&]() -> task<> {
        gates[1].set();
        co_return;
    };

    auto [result, drove] = run(combined(), driver());
    ZASSERT(result.has_value());
    ZASSERT(result->index() == 1U);
    ZEXPECT(std::get<1>(*result) == 10);
    ZEXPECT(finished == 1);
    ZEXPECT(!gates[0].has_waiters());
}

ZEST_CASE(synchronous_winner_keeps_later_children_from_starting) {
    int started = 0;
    auto child = [&](int v) -> task<int> {
        started += 1;
        co_return v;
    };
    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(child(1), child(2));
    };

    auto [result] = run(combined());
    ZASSERT(result.has_value());
    ZASSERT(result->index() == 0U);
    ZEXPECT(std::get<0>(*result) == 1);
    ZEXPECT(started == 1);
}

ZEST_CASE(single_child_wins) {
    auto child = []() -> task<int> {
        co_return 99;
    };
    auto combined = [&]() -> task<std::variant<int>> {
        co_return co_await when_any(child());
    };

    auto [result] = run(combined());
    ZASSERT(result.has_value());
    ZEXPECT(std::get<0>(*result) == 99);
}

ZEST_CASE(accepts_awaiters_that_are_not_tasks) {
    semaphore slow;
    semaphore fast;
    auto combined = [&]() -> task<std::variant<std::nullopt_t, std::nullopt_t>> {
        co_return co_await when_any(slow.acquire(), fast.acquire());
    };
    auto releaser = [&]() -> task<> {
        fast.release();
        co_return;
    };

    auto [result, driver] = run(combined(), releaser());
    ZASSERT(result.has_value());
    ZEXPECT(result->index() == 1U);
    ZEXPECT(!slow.has_waiters());
}

ZEST_CASE(range_reports_the_winner_index) {
    event gates[3];
    auto child = [&](int id) -> task<int> {
        co_await gates[id].wait();
        co_return id * 10;
    };
    auto combined = [&]() -> task<std::pair<std::size_t, int>> {
        std::vector<task<int>> tasks;
        for(int id = 0; id < 3; ++id) {
            tasks.push_back(child(id));
        }
        co_return co_await when_any(std::move(tasks));
    };
    auto driver = [&]() -> task<> {
        gates[2].set();
        co_return;
    };

    auto [result, drove] = run(combined(), driver());
    ZASSERT(result.has_value());
    ZEXPECT(result->first == 2U);
    ZEXPECT(result->second == 20);
}

ZEST_CASE(range_with_one_element_wins_at_once) {
    auto child = []() -> task<int> {
        co_return 42;
    };
    auto combined = [&]() -> task<std::pair<std::size_t, int>> {
        small_vector<task<int>> tasks;
        tasks.emplace_back(child());
        co_return co_await when_any(std::move(tasks));
    };

    auto [result] = run(combined());
    ZASSERT(result.has_value());
    ZEXPECT(result->first == 0U);
    ZEXPECT(result->second == 42);
}

ZEST_CASE(range_of_awaiters_that_are_not_tasks) {
    semaphore slow;
    semaphore fast;
    auto combined = [&]() -> task<std::size_t> {
        small_vector<semaphore::acquire_awaiter> waits;
        waits.emplace_back(slow.acquire());
        waits.emplace_back(fast.acquire());
        auto winner = co_await when_any(std::move(waits));
        co_return winner.first;
    };
    auto releaser = [&]() -> task<> {
        fast.release();
        co_return;
    };

    auto [result, driver] = run(combined(), releaser());
    ZASSERT(result.has_value());
    ZEXPECT(*result == 1U);
    ZEXPECT(!slow.has_waiters());
}

#if KOTA_ENABLE_EXCEPTIONS
ZEST_CASE(empty_range_fails) {
    small_vector<task<int>> tasks;
    ZEXPECT(zest::throws<std::invalid_argument>([&] { (void)when_any(std::move(tasks)); }));
}
#endif

};  // ZEST_SUITE(async_runtime_when_any)

}  // namespace

}  // namespace kota
