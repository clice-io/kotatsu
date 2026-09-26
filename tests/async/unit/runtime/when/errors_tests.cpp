#include <cstddef>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

struct CustomError {
    int code = 0;
};

task<int, error> failure(error err) {
    co_await fail(err);
}

task<int, error> success(int value) {
    co_return value;
}

ZEST_SUITE(async_runtime_when_errors, test::LoopFixture) {

ZEST_CASE(all_first_error_cancels_the_rest) {
    event gate;
    event go;
    bool slow_finished = false;
    auto failing = [&]() -> task<int, error> {
        co_await go.wait();
        co_await fail(error::connection_refused);
    };
    auto slow = [&]() -> task<int, error> {
        co_await gate.wait();
        slow_finished = true;
        co_return 42;
    };
    auto combined = [&]() -> task<result<std::tuple<int, int>>> {
        co_return co_await when_all(failing(), slow());
    };
    auto driver = [&]() -> task<> {
        go.set();
        co_return;
    };

    auto [result, drove] = run(combined(), driver());
    ASSERT(result.has_value());
    ASSERT(result->has_error());
    EXPECT(result->error() == error::connection_refused);
    EXPECT(!slow_finished);
    EXPECT(gate.get_head() == nullptr);
}

ZEST_CASE(all_error_while_armed_starts_no_later_child) {
    int started = 0;
    auto later = [&]() -> task<int, error> {
        started += 1;
        co_return 1;
    };
    auto combined = [&]() -> task<result<std::tuple<int, int>>> {
        co_return co_await when_all(failure(error::connection_refused), later());
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    ASSERT(result->has_error());
    EXPECT(result->error() == error::connection_refused);
    EXPECT(started == 0);
}

ZEST_CASE(all_success_has_no_error) {
    auto combined = []() -> task<result<std::tuple<int, int>>> {
        co_return co_await when_all(success(1), success(2));
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    ASSERT(result->has_value());
    EXPECT(**result == std::tuple{1, 2});
}

ZEST_CASE(any_first_error_wins_and_cancels_the_rest) {
    event gate;
    event go;
    bool slow_finished = false;
    auto failing = [&]() -> task<int, error> {
        co_await go.wait();
        co_await fail(error::connection_refused);
    };
    auto slow = [&]() -> task<int, error> {
        co_await gate.wait();
        slow_finished = true;
        co_return 42;
    };
    auto combined = [&]() -> task<result<std::variant<int, int>>> {
        co_return co_await when_any(failing(), slow());
    };
    auto driver = [&]() -> task<> {
        go.set();
        co_return;
    };

    auto [result, drove] = run(combined(), driver());
    ASSERT(result.has_value());
    ASSERT(result->has_error());
    EXPECT(result->error() == error::connection_refused);
    EXPECT(!slow_finished);
}

ZEST_CASE(any_first_of_several_errors_wins) {
    auto combined = []() -> task<result<std::variant<int, int>>> {
        co_return co_await when_any(failure(error::connection_refused),
                                    failure(error::end_of_file));
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    ASSERT(result->has_error());
    EXPECT(result->error() == error::connection_refused);
}

ZEST_CASE(all_range_error_cancels_the_rest) {
    event gate;
    bool slow_finished = false;
    auto slow = [&]() -> task<int, error> {
        co_await gate.wait();
        slow_finished = true;
        co_return 42;
    };
    auto combined = [&]() -> task<result<small_vector<int>>> {
        std::vector<task<int, error>> tasks;
        tasks.push_back(slow());
        tasks.push_back(failure(error::connection_refused));
        co_return co_await when_all(std::move(tasks));
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    ASSERT(result->has_error());
    EXPECT(result->error() == error::connection_refused);
    EXPECT(!slow_finished);
}

ZEST_CASE(any_range_error_wins) {
    event gate;
    auto slow = [&]() -> task<int, error> {
        co_await gate.wait();
        co_return 42;
    };
    auto combined = [&]() -> task<result<std::pair<std::size_t, int>>> {
        std::vector<task<int, error>> tasks;
        tasks.push_back(slow());
        tasks.push_back(failure(error::connection_refused));
        co_return co_await when_any(std::move(tasks));
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    ASSERT(result->has_error());
    EXPECT(result->error() == error::connection_refused);
    EXPECT(gate.get_head() == nullptr);
}

ZEST_CASE(all_range_success_has_no_error) {
    auto combined = []() -> task<result<small_vector<int>>> {
        std::vector<task<int, error>> tasks;
        tasks.push_back(success(1));
        tasks.push_back(success(2));
        tasks.push_back(success(3));
        co_return co_await when_all(std::move(tasks));
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    ASSERT(result->has_value());
    auto& values = **result;
    EXPECT(std::vector<int>(values.begin(), values.end()) == std::vector{1, 2, 3});
}

ZEST_CASE(mixed_error_types_come_back_as_a_variant) {
    // The error variant lists the children's error types in their order.
    using AllErrors = std::variant<error, CustomError>;
    using AnyErrors = std::variant<CustomError, error>;
    event gate;
    auto custom = []() -> task<int, CustomError> {
        co_await fail(CustomError{7});
    };
    auto slow = [&]() -> task<int, error> {
        co_await gate.wait();
        co_return 42;
    };
    auto all = [&]() -> task<outcome<std::tuple<int, int>, AllErrors>> {
        co_return co_await when_all(failure(error::connection_refused), custom());
    };
    auto any = [&]() -> task<outcome<std::variant<int, int>, AnyErrors>> {
        co_return co_await when_any(custom(), slow());
    };

    auto [all_result, any_result] = run(all(), any());
    ASSERT(all_result.has_value());
    ASSERT(all_result->has_error());
    ASSERT(std::holds_alternative<error>(all_result->error()));
    EXPECT(std::get<error>(all_result->error()) == error::connection_refused);
    ASSERT(any_result.has_value());
    ASSERT(any_result->has_error());
    ASSERT(std::holds_alternative<CustomError>(any_result->error()));
    EXPECT(std::get<CustomError>(any_result->error()).code == 7);
}

// A sibling's cancel decides the outcome first; a child that fails while it
// is being cancelled still turns it into its error.
ZEST_CASE(error_outranks_a_sibling_cancel) {
    event gate;
    auto failing_when_cancelled = [&]() -> task<int, error, cancellation> {
        co_await gate.wait().catch_cancel();
        co_await fail(error::connection_refused);
    };
    auto cancelling = []() -> task<int, error, cancellation> {
        co_await cancel();
        co_return 0;
    };
    auto combined = [&]() -> task<outcome<std::tuple<int, int>, error, cancellation>> {
        co_return co_await when_all(failing_when_cancelled(), cancelling());
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    ASSERT(result->has_error());
    EXPECT(result->error() == error::connection_refused);
}

// A child that cancels the whole scope and then fails still reports its
// error: a racing cancellation never masks a real error.
ZEST_CASE(error_outranks_an_external_cancel) {
    event gate;
    event go;
    async_node* scope = nullptr;
    auto failing = [&]() -> task<int, error, cancellation> {
        co_await go.wait();
        scope->cancel();
        co_await fail(error::connection_refused);
    };
    auto slow = [&]() -> task<int, error, cancellation> {
        co_await gate.wait();
        co_return 1;
    };
    std::optional<outcome<std::tuple<int, int>, error, cancellation>> seen;
    auto combined = [&]() -> task<> {
        seen.emplace(co_await when_all(failing(), slow()));
    };
    auto target = combined();
    scope = target.operator->();
    auto driver = [&]() -> task<> {
        go.set();
        co_return;
    };

    auto [result, drove] = run(std::move(target), driver());
    ASSERT(seen.has_value());
    ASSERT(seen->has_error());
    EXPECT(seen->error() == error::connection_refused);
    // The scope still ends cancelled once it has seen the error.
    EXPECT(result.is_cancelled());
}

};  // ZEST_SUITE(async_runtime_when_errors)

}  // namespace

}  // namespace kota
