#include <chrono>
#include <vector>

#include "kota/zest/async.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::zest {

namespace {

ZEST_SUITE(zest_async, LoopFixture) {

ZEST_CASE(run_returns_what_each_task_ended_with) {
    auto value = []() -> task<int> {
        co_return 7;
    };
    auto failure = []() -> task<int, error> {
        co_await fail(error::io_error);
        co_return 0;
    };
    auto [seven, failed] = run(value(), failure());
    ASSERT(seven.has_value());
    EXPECT(*seven == 7);
    ASSERT(failed.has_error());
    EXPECT(failed.error() == error::io_error);
}

ZEST_CASE(run_starts_the_tasks_in_order) {
    std::vector<int> started;
    auto starts = [&](int index) -> task<> {
        started.push_back(index);
        co_return;
    };
    run(starts(1), starts(2), starts(3));
    EXPECT(started == std::vector{1, 2, 3});
}

ZEST_CASE(kept_task_can_be_cancelled_by_the_test) {
    event gate;
    auto waits = [&]() -> task<> {
        co_await gate.wait();
    };
    auto kept = waits();
    auto cancels = [&]() -> task<> {
        kept.cancel();
        co_return;
    };
    auto [waited, cancelled] = run(kept, cancels());
    EXPECT(waited.is_cancelled());
    EXPECT(cancelled.has_value());
}

ZEST_CASE(watchdog_defaults_to_ten_seconds) {
    EXPECT(watchdog.count() == 10000);
}

};  // ZEST_SUITE(zest_async)

}  // namespace

}  // namespace kota::zest
