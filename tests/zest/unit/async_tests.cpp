#include <chrono>
#include <stdexcept>
#include <vector>

#include "async/harness/exceptions.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/async/async.h"

namespace kota::zest {

namespace {

using namespace std::literals;

ZEST_SUITE(zest_async, LoopFixture) {

ZEST_CASE(value_comes_back_as_an_outcome) {
    auto one = []() -> task<int> {
        co_return 1;
    };
    auto kept = one();

    auto [owned, borrowed] = run(one(), kept);
    EXPECT(type_eq<decltype(owned), outcome<int, void, cancellation>>());
    EXPECT(type_eq<decltype(borrowed), outcome<int, void, cancellation>>());
    ASSERT(owned.has_value());
    EXPECT(*owned == 1);
    ASSERT(borrowed.has_value());
    EXPECT(*borrowed == 1);
}

ZEST_CASE(error_comes_back_as_an_outcome) {
    auto refused = []() -> task<int, error> {
        co_await fail(error::connection_refused);
    };
    auto aborted = []() -> task<void, error> {
        co_await fail(error::operation_aborted);
    };
    auto kept = aborted();

    auto [owned, borrowed] = run(refused(), kept);
    ASSERT(owned.has_error());
    EXPECT(owned.error() == error::connection_refused);
    ASSERT(borrowed.has_error());
    EXPECT(borrowed.error() == error::operation_aborted);
}

ZEST_CASE(cancellation_comes_back_as_an_outcome) {
    auto quit = []() -> task<int> {
        co_await cancel();
        co_return 1;
    };

    auto [result] = run(quit());
    EXPECT(result.is_cancelled());
}

// The gate is never set, so only the test's cancel() can end the kept task.
ZEST_CASE(kept_task_can_be_cancelled_while_it_runs) {
    event gate;
    auto waiter = [&]() -> task<> {
        co_await gate.wait();
    };
    auto kept = waiter();
    auto canceller = [&]() -> task<> {
        kept.cancel();
        co_return;
    };

    auto [result, cancelled] = run(kept, canceller());
    EXPECT(result.is_cancelled());
    EXPECT(kept.is_cancelled());
    EXPECT(cancelled.has_value());
}

ZEST_CASE(tasks_start_in_the_order_given) {
    std::vector<int> started;
    auto step = [&](int id) -> task<> {
        started.push_back(id);
        co_return;
    };
    auto kept = step(2);

    run(step(1), kept, step(3));
    EXPECT(started == std::vector{1, 2, 3});
}

// The timer is armed for an hour, so only the last task finishing can end
// run().
ZEST_CASE(loop_stops_when_the_last_task_finishes_with_a_timer_armed) {
    auto armed = timer::create(loop);
    ASSERT(!armed.start(1h));
    auto quick = []() -> task<int> {
        co_return 1;
    };

    auto [result] = run(quick());
    ASSERT(result.has_value());
    EXPECT(*result == 1);
}

#if KOTA_ENABLE_EXCEPTIONS

// run() lets every other task finish before it fails with what was thrown,
// which it reads; see test::exceptions_unreadable.
ZEST_CASE(run_of_a_throwing_task_fails, skip = test::exceptions_unreadable) {
    bool finished = false;
    auto thrower = []() -> task<> {
        throw std::runtime_error("boom");
        co_return;
    };
    auto later = [&]() -> task<> {
        co_await yield(loop);
        finished = true;
    };

    EXPECT(test::thrown([&] { run(thrower(), later()); }) == "boom");
    EXPECT(finished);
}

#endif  // KOTA_ENABLE_EXCEPTIONS

};  // ZEST_SUITE(zest_async)

}  // namespace

}  // namespace kota::zest
