#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "async/harness/pending_op.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/async/async.h"

namespace kota {

namespace {

struct AppError {
    int code = 0;
    std::string detail;

    AppError(int code, std::string detail) : code(code), detail(std::move(detail)) {}
};

ZEST_SUITE(async_runtime_task, test::LoopFixture) {

ZEST_CASE(await_returns_the_child_value) {
    auto one = []() -> task<int> {
        co_return 1;
    };
    auto two = [&]() -> task<int> {
        co_return co_await one() + 1;
    };
    auto three = [&]() -> task<int> {
        auto a = co_await one();
        auto b = co_await two();
        co_return a + b;
    };

    auto [result] = run(three());
    ASSERT(result.has_value());
    EXPECT(*result == 3);
}

ZEST_CASE(await_of_a_void_child_resumes_after_it) {
    std::vector<int> order;
    auto child = [&]() -> task<> {
        order.push_back(1);
        co_return;
    };
    auto parent = [&]() -> task<> {
        co_await child();
        order.push_back(2);
    };

    auto [result] = run(parent());
    EXPECT(result.has_value());
    EXPECT(order == std::vector{1, 2});
}

ZEST_CASE(fail_ends_the_task_with_the_error) {
    bool after = false;
    auto failing = [&]() -> task<int, error> {
        co_await fail(error::io_error);
        after = true;
        co_return 1;
    };

    auto [result] = run(failing());
    ASSERT(result.has_error());
    EXPECT(result.error() == error::io_error);
    EXPECT(!after);
}

ZEST_CASE(fail_builds_the_error_from_its_arguments) {
    auto failing = []() -> task<void, AppError> {
        co_await fail(7, "bad input");
    };

    auto [result] = run(failing());
    ASSERT(result.has_error());
    EXPECT(result.error().code == 7);
    EXPECT(result.error().detail == "bad input");
}

ZEST_CASE(await_of_a_failing_child_hands_its_error_to_the_parent) {
    auto child = []() -> task<int, error> {
        co_await fail(error::connection_refused);
    };
    auto parent = [&]() -> task<result<int>> {
        co_return co_await child();
    };

    auto [result] = run(parent());
    ASSERT(result.has_value());
    ASSERT(result->has_error());
    EXPECT(result->error() == error::connection_refused);
}

ZEST_CASE(or_fail_unwraps_a_successful_outcome) {
    auto parent = []() -> task<int, error> {
        co_await or_fail(result<void>());
        auto value = co_await or_fail(result<int>(5));
        co_return value * 2;
    };

    auto [result] = run(parent());
    ASSERT(result.has_value());
    EXPECT(*result == 10);
}

ZEST_CASE(or_fail_ends_the_task_with_a_failed_outcome) {
    bool after = false;
    auto parent = [&]() -> task<int, error> {
        result<int> failed = outcome_error(error::invalid_argument);
        auto value = co_await or_fail(std::move(failed));
        after = true;
        co_return value;
    };

    auto [result] = run(parent());
    ASSERT(result.has_error());
    EXPECT(result.error() == error::invalid_argument);
    EXPECT(!after);
}

ZEST_CASE(or_fail_on_a_task_unwraps_its_value) {
    auto child = []() -> task<int, error> {
        co_return 5;
    };
    auto parent = [&]() -> task<int, error> {
        co_return co_await child().or_fail() * 2;
    };

    auto [result] = run(parent());
    ASSERT(result.has_value());
    EXPECT(*result == 10);
}

ZEST_CASE(or_fail_on_a_task_ends_the_parent_without_resuming_it) {
    bool resumed = false;
    auto child = []() -> task<int, error> {
        co_await fail(error::connection_reset_by_peer);
    };
    auto parent = [&]() -> task<int, error> {
        auto value = co_await child().or_fail();
        resumed = true;
        co_return value;
    };

    auto [result] = run(parent());
    ASSERT(result.has_error());
    EXPECT(result.error() == error::connection_reset_by_peer);
    EXPECT(!resumed);
}

ZEST_CASE(or_fail_converts_the_error_to_the_parent_type) {
    using Errors = std::variant<error, AppError>;
    auto child = []() -> task<int, error> {
        co_await fail(error::connection_timed_out);
    };
    auto from_task = [&]() -> task<int, Errors> {
        co_return co_await child().or_fail();
    };
    auto from_outcome = []() -> task<int, Errors> {
        result<int> failed = outcome_error(error::broken_pipe);
        co_return co_await or_fail(std::move(failed));
    };

    auto [by_task, by_outcome] = run(from_task(), from_outcome());
    ASSERT(by_task.has_error());
    ASSERT(std::holds_alternative<error>(by_task.error()));
    EXPECT(std::get<error>(by_task.error()) == error::connection_timed_out);
    ASSERT(by_outcome.has_error());
    ASSERT(std::holds_alternative<error>(by_outcome.error()));
    EXPECT(std::get<error>(by_outcome.error()) == error::broken_pipe);
}

ZEST_CASE(cancel_ends_the_task_cancelled) {
    bool after = false;
    auto worker = [&]() -> task<int> {
        co_await cancel();
        after = true;
        co_return 1;
    };

    auto [result] = run(worker());
    EXPECT(result.is_cancelled());
    EXPECT(!after);
}

ZEST_CASE(cancelled_child_cancels_its_parent) {
    int steps = 0;
    auto child = [&]() -> task<> {
        steps += 1;
        co_await cancel();
    };
    auto parent = [&]() -> task<> {
        co_await child();
        steps += 1;
    };

    auto [result] = run(parent());
    EXPECT(result.is_cancelled());
    EXPECT(steps == 1);
}

ZEST_CASE(catch_cancel_hands_the_cancellation_to_the_parent) {
    EXPECT(zest::type_eq<decltype(std::declval<task<int, error>>().catch_cancel()),
                         task<int, error, cancellation>>());

    auto child = []() -> task<int> {
        co_await cancel();
        co_return 1;
    };
    auto parent = [&]() -> task<outcome<int, void, cancellation>> {
        co_return co_await child().catch_cancel();
    };

    auto [result] = run(parent());
    ASSERT(result.has_value());
    EXPECT(result->is_cancelled());
}

ZEST_CASE(catch_cancel_passes_values_and_errors_through) {
    using Caught = outcome<int, error, cancellation>;
    auto value = []() -> task<int, error> {
        co_return 3;
    };
    auto failing = []() -> task<int, error> {
        co_await fail(error::io_error);
    };
    auto parent = [&]() -> task<std::pair<Caught, Caught>> {
        auto first = co_await value().catch_cancel();
        auto second = co_await failing().catch_cancel();
        co_return std::pair{std::move(first), std::move(second)};
    };

    auto [result] = run(parent());
    ASSERT(result.has_value());
    auto& [first, second] = *result;
    ASSERT(first.has_value());
    EXPECT(*first == 3);
    ASSERT(second.has_error());
    EXPECT(second.error() == error::io_error);
}

ZEST_CASE(external_cancel_ends_a_suspended_task) {
    event gate;
    bool after = false;
    auto worker = [&]() -> task<int> {
        co_await gate.wait();
        after = true;
        co_return 1;
    };
    auto target = worker();
    auto* node = target.operator->();
    auto cancel_it = [&]() -> task<> {
        node->cancel();
        co_return;
    };

    auto [result, driver] = run(std::move(target), cancel_it());
    EXPECT(result.is_cancelled());
    EXPECT(!after);
    EXPECT(driver.has_value());
}

// A task cancelled while it runs goes on until its next suspending co_await,
// which ends it instead of starting new work.
ZEST_CASE(checkpoint_starts_no_child_after_cancel) {
    int started = 0;
    async_node* self = nullptr;
    auto child = [&]() -> task<> {
        started += 1;
        co_return;
    };
    auto worker = [&]() -> task<> {
        for(int i = 0; i < 100; ++i) {
            if(i == 3) {
                self->cancel();
            }
            co_await child();
        }
    };
    auto target = worker();
    self = target.operator->();

    auto [result] = run(std::move(target));
    EXPECT(result.is_cancelled());
    EXPECT(started == 3);
}

ZEST_CASE(checkpoint_waits_for_the_cancelled_operation) {
    test::PendingOp op;
    async_node* self = nullptr;
    bool worker_done = false;
    auto worker = [&]() -> task<> {
        self->cancel();
        co_await op;
    };
    auto target = worker();
    self = target.operator->();
    auto observe = [&]() -> task<> {
        co_await std::move(target).catch_cancel();
        worker_done = true;
    };
    auto finish = [&]() -> task<bool> {
        bool done_before = worker_done;
        op.complete();
        co_return done_before;
    };

    auto [observer, driver] = run(observe(), finish());
    EXPECT(op.is_cancelled());
    ASSERT(driver.has_value());
    EXPECT(!*driver);
    EXPECT(worker_done);
    EXPECT(observer.has_value());
}

ZEST_CASE(error_after_cancel_is_still_reported) {
    async_node* self = nullptr;
    auto worker = [&]() -> task<int, error> {
        self->cancel();
        co_await fail(error::io_error);
    };
    auto target = worker();
    self = target.operator->();

    auto [result] = run(std::move(target));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::io_error);
}

ZEST_CASE(value_after_cancel_is_dropped) {
    async_node* self = nullptr;
    auto worker = [&]() -> task<int> {
        self->cancel();
        co_return 1;
    };
    auto target = worker();
    self = target.operator->();

    auto [result] = run(std::move(target));
    EXPECT(result.is_cancelled());
}

// A child started by co_await runs until it first suspends; a cancel() that
// reaches it meanwhile waits for that point instead of resuming the parent
// under the running child.
ZEST_CASE(child_cancelled_before_it_suspends_runs_to_that_point) {
    async_node* child_node = nullptr;
    bool child_finished = false;
    auto child = [&]() -> task<int> {
        child_node->cancel();
        child_finished = true;
        co_return 1;
    };
    // What the parent sees when it resumes: the child's outcome, and whether
    // the child had run to its end by then.
    auto parent = [&]() -> task<std::pair<bool, bool>> {
        auto started = child();
        child_node = started.operator->();
        auto result = co_await std::move(started).catch_cancel();
        co_return std::pair{result.is_cancelled(), child_finished};
    };

    auto [result] = run(parent());
    ASSERT(result.has_value());
    EXPECT(result->first);
    EXPECT(result->second);
}

// Open question, kept to document current behaviour: cancel() on a task that
// has not started only marks it, and awaiting it afterwards starts it as if it
// had never been cancelled. A scheduled root cancelled the same way never runs
// (async_io_loop.task_cancelled_before_it_starts_never_runs).
ZEST_CASE(awaiting_a_task_cancelled_before_it_started_runs_it) {
    bool ran = false;
    auto child = [&]() -> task<int> {
        ran = true;
        co_return 1;
    };
    auto parent = [&]() -> task<int> {
        auto pending = child();
        pending->cancel();
        co_return co_await std::move(pending);
    };

    auto [result] = run(parent());
    ASSERT(result.has_value());
    EXPECT(*result == 1);
    EXPECT(ran);
}

#if KOTA_ENABLE_EXCEPTIONS

ZEST_CASE(exception_propagates_through_await) {
    auto thrower = []() -> task<int> {
        throw std::runtime_error("boom");
        co_return 0;
    };
    auto parent = [&]() -> task<int> {
        co_return co_await thrower();
    };

    EXPECT_THROWS(run(parent()));
}

ZEST_CASE(parent_can_catch_a_child_exception) {
    auto thrower = []() -> task<int> {
        throw std::runtime_error("caught");
        co_return 0;
    };
    auto parent = [&]() -> task<std::string> {
        try {
            co_await thrower();
        } catch(const std::runtime_error& e) {
            co_return e.what();
        }
        co_return "";
    };

    auto [result] = run(parent());
    ASSERT(result.has_value());
    EXPECT(*result == "caught");
}

ZEST_CASE(or_fail_rethrows_a_child_exception) {
    auto child = []() -> task<int, error> {
        throw std::runtime_error("or_fail child");
        co_return 0;
    };
    auto parent = [&]() -> task<int, error> {
        co_return co_await child().or_fail();
    };

    EXPECT_THROWS(run(parent()));
}

// Real errors outrank cancellation: an exception thrown after the task was
// cancelled still fails it.
ZEST_CASE(exception_after_cancel_still_fails_the_task) {
    async_node* self = nullptr;
    auto worker = [&]() -> task<> {
        self->cancel();
        throw std::runtime_error("after cancel");
        co_return;
    };
    auto target = worker();
    self = target.operator->();

    EXPECT_THROWS(run(std::move(target)));
}

#endif  // KOTA_ENABLE_EXCEPTIONS

};  // ZEST_SUITE(async_runtime_task)

}  // namespace

}  // namespace kota
