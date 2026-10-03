#include <chrono>

#include "async/harness/pending_op.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

using namespace std::literals;

/// Waits on `gate` in a task of its own, whose cancellation the caller can
/// catch.
task<> wait_on(event& gate) {
    co_await gate.wait();
}

ZEST_SUITE(async_io_watcher_timeout, zest::LoopFixture) {

ZEST_CASE(task_that_ends_in_time_gives_its_value) {
    auto quick = []() -> task<int> {
        co_await yield();
        co_return 42;
    };

    auto [result] = run(with_timeout(quick(), 1h, loop));
    ASSERT(result.has_value());
    EXPECT(*result == 42);
}

ZEST_CASE(task_that_fails_in_time_gives_its_error) {
    auto failing = []() -> task<int, error> {
        co_await yield();
        co_await fail(error::connection_refused);
    };

    auto [result] = run(with_timeout(failing(), 1h, loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::connection_refused);
}

ZEST_CASE(task_that_cancels_itself_reports_the_cancellation) {
    auto cancelling = []() -> task<int> {
        co_await yield();
        co_await cancel();
    };

    auto [result] = run(with_timeout(cancelling(), 1h, loop));
    EXPECT(result.is_cancelled());
}

// The deadline's cancel reaches the operation the task waits on, which
// completes only when the test says so: the result comes only after that.
ZEST_CASE(deadline_cancels_the_task_and_waits_for_it) {
    test::PendingOp op;
    bool returned = false;
    auto pending = [&]() -> task<> {
        co_await op;
    };
    auto guarded = [&]() -> task<bool> {
        auto result = co_await with_timeout(pending(), 1ms);
        returned = true;
        co_return result.is_cancelled();
    };
    auto finisher = [&]() -> task<bool> {
        // Turns of the loop until the deadline has passed.
        while(!op.cancel_requested()) {
            co_await yield();
        }
        bool returned_before = returned;
        op.complete();
        co_return returned_before;
    };

    auto [cancelled, returned_before] = run(guarded(), finisher());
    ASSERT(cancelled.has_value());
    EXPECT(*cancelled);
    ASSERT(returned_before.has_value());
    EXPECT(!*returned_before);
}

// A task that the deadline cancels and that fails then reports the error.
ZEST_CASE(error_while_the_deadline_cancels_is_reported) {
    event gate;
    auto failing_when_cancelled = [&]() -> task<void, error> {
        co_await wait_on(gate).catch_cancel();
        co_await fail(error::connection_reset_by_peer);
    };

    auto [result] = run(with_timeout(failing_when_cancelled(), 1ms, loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::connection_reset_by_peer);
}

// A cancel of the task awaiting with_timeout() reaches the task it runs.
ZEST_CASE(cancel_from_outside_reaches_the_task) {
    event gate;
    auto waiting = [&]() -> task<> {
        co_await gate.wait();
    };
    auto guarded = [&]() -> task<> {
        [[maybe_unused]] auto result = co_await with_timeout(waiting(), 1h);
    };
    auto target = guarded();
    auto canceler = [&]() -> task<> {
        target.cancel();
        co_return;
    };

    auto [result, cancelled] = run(target, canceler());
    EXPECT(result.is_cancelled());
    EXPECT(!gate.has_waiters());
}

};  // ZEST_SUITE(async_io_watcher_timeout)

}  // namespace

}  // namespace kota
