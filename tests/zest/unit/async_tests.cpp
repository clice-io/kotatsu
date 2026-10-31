#include <chrono>
#include <stdexcept>
#include <vector>

#include "support/harness/throws.h"
#include "kotatsu/zest/async.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/support/config.h"
#include "kotatsu/async/async.h"

namespace kotatsu::zest {

namespace {

using namespace std::literals;

ZEST_SUITE(zest_async, LoopFixture) {

ZEST_CASE(value_comes_back_as_an_outcome) {
    auto one = []() -> async::task<int> {
        co_return 1;
    };
    auto kept = one();

    auto [owned, borrowed] = run(one(), kept);
    ZEXPECT(type_eq<decltype(owned), outcome<int, void, async::cancellation>>());
    ZEXPECT(type_eq<decltype(borrowed), outcome<int, void, async::cancellation>>());
    ZASSERT(owned.has_value());
    ZEXPECT(*owned == 1);
    ZASSERT(borrowed.has_value());
    ZEXPECT(*borrowed == 1);
}

ZEST_CASE(error_comes_back_as_an_outcome) {
    auto refused = []() -> async::task<int, async::error> {
        co_await async::fail(async::error::connection_refused);
    };
    auto aborted = []() -> async::task<void, async::error> {
        co_await async::fail(async::error::operation_aborted);
    };
    auto kept = aborted();

    auto [owned, borrowed] = run(refused(), kept);
    ZASSERT(owned.has_error());
    ZEXPECT(owned.error() == async::error::connection_refused);
    ZASSERT(borrowed.has_error());
    ZEXPECT(borrowed.error() == async::error::operation_aborted);
}

ZEST_CASE(cancellation_comes_back_as_an_outcome) {
    auto quit = []() -> async::task<int> {
        co_await async::cancel();
        co_return 1;
    };

    auto [result] = run(quit());
    ZEXPECT(result.is_cancelled());
}

// The gate is never set, so only the test's cancel() can end the kept task.
ZEST_CASE(kept_task_can_be_cancelled_while_it_runs) {
    async::event gate;
    auto waiter = [&]() -> async::task<> {
        co_await gate.wait();
    };
    auto kept = waiter();
    auto canceller = [&]() -> async::task<> {
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
    auto step = [&](int id) -> async::task<> {
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
    auto armed = async::timer::create(loop);
    ZASSERT(!armed.start(1h));
    auto quick = []() -> async::task<int> {
        co_return 1;
    };

    auto [result] = run(quick());
    ZASSERT(result.has_value());
    ZEXPECT(*result == 1);
}

#if KOTATSU_ENABLE_EXCEPTIONS

// run() lets every other task finish before it fails with what was thrown.
ZEST_CASE(run_of_a_throwing_task_fails) {
    bool finished = false;
    auto thrower = []() -> async::task<> {
        throw std::runtime_error("boom");
        co_return;
    };
    auto later = [&]() -> async::task<> {
        co_await async::yield(loop);
        finished = true;
    };

    ZEXPECT(test::thrown([&] { run(thrower(), later()); }) == "boom");
    ZEXPECT(finished);
}

#endif  // KOTATSU_ENABLE_EXCEPTIONS

};  // ZEST_SUITE(zest_async)

}  // namespace

}  // namespace kotatsu::zest
