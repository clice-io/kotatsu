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
    ZEXPECT(type_eq<decltype(owned), outcome<int, void, cancellation>>());
    ZEXPECT(type_eq<decltype(borrowed), outcome<int, void, cancellation>>());
    ZASSERT(owned.has_value());
    ZEXPECT(*owned == 1);
    ZASSERT(borrowed.has_value());
    ZEXPECT(*borrowed == 1);
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
    ZASSERT(owned.has_error());
    ZEXPECT(owned.error() == error::connection_refused);
    ZASSERT(borrowed.has_error());
    ZEXPECT(borrowed.error() == error::operation_aborted);
}

ZEST_CASE(cancellation_comes_back_as_an_outcome) {
    auto quit = []() -> task<int> {
        co_await cancel();
        co_return 1;
    };

    auto [result] = run(quit());
    ZEXPECT(result.is_cancelled());
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
    ZEXPECT(result.is_cancelled());
    ZEXPECT(kept.is_cancelled());
    ZEXPECT(cancelled.has_value());
}

ZEST_CASE(tasks_start_in_the_order_given) {
    std::vector<int> started;
    auto step = [&](int id) -> task<> {
        started.push_back(id);
        co_return;
    };
    auto kept = step(2);

    run(step(1), kept, step(3));
    ZEXPECT(started == std::vector{1, 2, 3});
}

// The timer is armed for an hour, so only the last task finishing can end
// run().
ZEST_CASE(loop_stops_when_the_last_task_finishes_with_a_timer_armed) {
    auto armed = timer::create(loop);
    ZASSERT(!armed.start(1h));
    auto quick = []() -> task<int> {
        co_return 1;
    };

    auto [result] = run(quick());
    ZASSERT(result.has_value());
    ZEXPECT(*result == 1);
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

    ZEXPECT(test::thrown([&] { run(thrower(), later()); }) == "boom");
    ZEXPECT(finished);
}

#endif  // KOTA_ENABLE_EXCEPTIONS

};  // ZEST_SUITE(zest_async)

}  // namespace

}  // namespace kota::zest
