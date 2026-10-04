#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "async/harness/exceptions.h"
#include "async/harness/pending_op.h"
#include "kota/zest/async.h"
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

struct Counter {
    task<int> next();
};

/// Whether co_invoke() takes `Fn` with `Args`.
template <typename Fn, typename... Args>
concept invocable_through_co_invoke =
    requires(Fn fn, Args... args) { co_invoke(std::move(fn), std::move(args)...); };

ZEST_SUITE(async_runtime_task, zest::LoopFixture) {

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

// Awaited as an lvalue, a task keeps its frame: its owner can still ask
// whether it is done, and the frame goes with the owner.
ZEST_CASE(await_of_an_lvalue_keeps_the_frame) {
    auto frame = std::make_shared<int>();
    std::weak_ptr<int> watch = frame;
    auto child = [](std::shared_ptr<int>) -> task<int> {
        co_return 5;
    };

    struct Seen {
        int value = 0;
        bool alive_after_await = false;
        bool done = false;
    };

    auto parent = [&]() -> task<Seen> {
        auto kept = child(std::move(frame));
        Seen seen;
        seen.value = co_await kept;
        seen.alive_after_await = !watch.expired();
        seen.done = kept.done();
        co_return seen;
    };

    auto [result] = run(parent());
    ASSERT(result.has_value());
    EXPECT(result->value == 5);
    EXPECT(result->alive_after_await);
    EXPECT(result->done);
    EXPECT(watch.expired());
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
    auto worker = [&]() -> task<int> {
        co_await gate.wait();
        co_return 1;
    };
    auto target = worker();
    auto cancel_it = [&]() -> task<bool> {
        bool done_before = target.done();
        target.cancel();
        co_return done_before;
    };

    auto [result, done_before] = run(target, cancel_it());
    EXPECT(result.is_cancelled());
    EXPECT(target.done());
    EXPECT(target.is_cancelled());
    EXPECT(!gate.has_waiters());
    ASSERT(done_before.has_value());
    EXPECT(!*done_before);
}

ZEST_CASE(cancel_of_a_finished_task_changes_nothing) {
    auto quick = []() -> task<int> {
        co_return 1;
    };
    auto target = quick();
    auto late = [&]() -> task<> {
        co_await yield();
        target.cancel();
    };

    auto [result, drove] = run(target, late());
    ASSERT(result.has_value());
    EXPECT(*result == 1);
    EXPECT(!target.is_cancelled());
    EXPECT(drove.has_value());
}

// A task cancelled while it runs goes on until its next suspending co_await,
// which ends it instead of starting new work.
ZEST_CASE(checkpoint_starts_no_child_after_cancel) {
    int started = 0;
    task<> target;
    auto child = [&]() -> task<> {
        started += 1;
        co_return;
    };
    auto worker = [&]() -> task<> {
        for(int i = 0; i < 100; ++i) {
            if(i == 3) {
                target.cancel();
            }
            co_await child();
        }
    };
    target = worker();

    auto [result] = run(target);
    EXPECT(result.is_cancelled());
    EXPECT(started == 3);
}

ZEST_CASE(checkpoint_waits_for_the_cancelled_operation) {
    test::PendingOp op;
    bool worker_done = false;
    task<void, void, cancellation> target;
    auto worker = [&]() -> task<> {
        target.cancel();
        co_await op;
    };
    target = worker().catch_cancel();
    auto observe = [&]() -> task<> {
        co_await target;
        worker_done = true;
    };
    auto finish = [&]() -> task<bool> {
        bool done_before = worker_done;
        op.complete();
        co_return done_before;
    };

    auto [observer, driver] = run(observe(), finish());
    EXPECT(op.cancel_requested());
    ASSERT(driver.has_value());
    EXPECT(!*driver);
    EXPECT(worker_done);
    EXPECT(observer.has_value());
}

ZEST_CASE(error_after_cancel_is_still_reported) {
    task<int, error> target;
    auto worker = [&]() -> task<int, error> {
        target.cancel();
        co_await fail(error::io_error);
    };
    target = worker();

    auto [result] = run(target);
    ASSERT(result.has_error());
    EXPECT(result.error() == error::io_error);
}

ZEST_CASE(value_after_cancel_is_dropped) {
    task<int> target;
    auto worker = [&]() -> task<int> {
        target.cancel();
        co_return 1;
    };
    target = worker();

    auto [result] = run(target);
    EXPECT(result.is_cancelled());
}

// A child started by co_await runs until it first suspends; a cancel() that
// reaches it meanwhile waits for that point instead of resuming the parent
// under the running child.
ZEST_CASE(child_cancelled_before_it_suspends_runs_to_that_point) {
    task<int, void, cancellation> started;
    bool child_finished = false;
    auto child = [&]() -> task<int> {
        started.cancel();
        child_finished = true;
        co_return 1;
    };
    // What the parent sees when it resumes: the child's outcome, and whether
    // the child had run to its end by then.
    auto parent = [&]() -> task<std::pair<bool, bool>> {
        started = child().catch_cancel();
        auto result = co_await started;
        co_return std::pair{result.is_cancelled(), child_finished};
    };

    auto [result] = run(parent());
    ASSERT(result.has_value());
    EXPECT(result->first);
    EXPECT(result->second);
}

// A task cancelled before it starts never runs: it ends cancelled at once,
// which cancels the task awaiting it unless that one catches it.
ZEST_CASE(awaiting_a_task_cancelled_before_it_started_never_runs_it) {
    bool ran = false;
    bool resumed = false;
    auto child = [&]() -> task<int> {
        ran = true;
        co_return 1;
    };
    auto catching = [&]() -> task<bool> {
        auto pending = child();
        pending.cancel();
        auto result = co_await std::move(pending).catch_cancel();
        co_return result.is_cancelled();
    };
    auto plain = [&]() -> task<int> {
        auto pending = child();
        pending.cancel();
        auto value = co_await std::move(pending);
        resumed = true;
        co_return value;
    };

    auto [caught, cancelled] = run(catching(), plain());
    ASSERT(caught.has_value());
    EXPECT(*caught);
    EXPECT(cancelled.is_cancelled());
    EXPECT(!ran);
    EXPECT(!resumed);
}

// Nothing runs the task; the cancel does not end it either.
ZEST_CASE(cancel_of_a_task_that_has_not_started_only_marks_it) {
    auto work = []() -> task<> {
        co_return;
    };
    auto pending = work();
    pending.cancel();

    EXPECT(!pending.done());
    EXPECT(!pending.is_cancelled());
}

#if KOTA_ENABLE_EXCEPTIONS

// Reads what was thrown; see test::exceptions_unreadable.
ZEST_CASE(exception_propagates_through_await, skip = test::exceptions_unreadable) {
    auto thrower = []() -> task<int> {
        throw std::runtime_error("boom");
        co_return 0;
    };
    auto parent = [&]() -> task<int> {
        co_return co_await thrower();
    };

    EXPECT(test::thrown([&] { run(parent()); }) == "boom");
}

// Reads what was thrown; see test::exceptions_unreadable.
ZEST_CASE(parent_can_catch_a_child_exception, skip = test::exceptions_unreadable) {
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

// Reads what was thrown; see test::exceptions_unreadable.
ZEST_CASE(or_fail_rethrows_a_child_exception, skip = test::exceptions_unreadable) {
    auto child = []() -> task<int, error> {
        throw std::runtime_error("or_fail child");
        co_return 0;
    };
    auto parent = [&]() -> task<int, error> {
        co_return co_await child().or_fail();
    };

    EXPECT(test::thrown([&] { run(parent()); }) == "or_fail child");
}

// Real errors outrank cancellation: an exception thrown after the task was
// cancelled still fails it.
// Reads what was thrown; see test::exceptions_unreadable.
ZEST_CASE(exception_after_cancel_still_fails_the_task, skip = test::exceptions_unreadable) {
    task<> target;
    auto worker = [&]() -> task<> {
        target.cancel();
        throw std::runtime_error("after cancel");
        co_return;
    };
    target = worker();

    EXPECT(test::thrown([&] { run(target); }) == "after cancel");
}

#endif  // KOTA_ENABLE_EXCEPTIONS

// The lambda is a temporary gone before the task starts: co_invoke keeps it
// for as long as the task runs.
ZEST_CASE(co_invoke_keeps_the_callable_for_the_task) {
    auto alive = std::make_shared<bool>(true);

    struct Probe {
        std::shared_ptr<bool> alive;

        Probe(std::shared_ptr<bool> alive) : alive(std::move(alive)) {}

        Probe(Probe&&) = default;

        ~Probe() {
            if(alive) {
                *alive = false;
            }
        }
    };

    auto read = co_invoke([&alive, probe = Probe(alive)]() -> task<bool> {
        co_await yield();
        co_return *alive;
    });

    auto [result] = run(std::move(read));
    ASSERT(result.has_value());
    EXPECT(*result);
}

ZEST_CASE(co_invoke_keeps_the_arguments_for_the_task) {
    auto read = [](const std::string& text) -> task<std::string> {
        co_await yield();
        co_return text;
    };
    auto invoked = co_invoke(read, std::string("an argument"));

    auto [result] = run(std::move(invoked));
    ASSERT(result.has_value());
    EXPECT(*result == "an argument");
}

ZEST_CASE(co_invoke_takes_what_can_be_called) {
    using member = task<int> (Counter::*)();
    STATIC_EXPECT(invocable_through_co_invoke<task<int> (*)()>);
    STATIC_EXPECT(!invocable_through_co_invoke<int (*)()>);
    STATIC_EXPECT(!invocable_through_co_invoke<task<int, void, cancellation> (*)()>);
    // A member function is no callable: bind it in a lambda.
    STATIC_EXPECT(!invocable_through_co_invoke<member, Counter*>);
}

ZEST_CASE(co_invoke_passes_the_error_through) {
    auto failing = []() -> task<void, error> {
        co_await fail(error::connection_refused);
    };

    auto [result] = run(co_invoke(failing));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::connection_refused);
}

ZEST_CASE(co_invoke_ends_cancelled_with_the_task) {
    auto cancelling = []() -> task<int> {
        co_await cancel();
    };

    auto [result] = run(co_invoke(cancelling));
    EXPECT(result.is_cancelled());
}

};  // ZEST_SUITE(async_runtime_task)

}  // namespace

}  // namespace kota
