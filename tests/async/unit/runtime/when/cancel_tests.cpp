#include <cstddef>
#include <memory>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "async/harness/pending_op.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

task<int> cancelled_value() {
    co_await cancel();
    co_return 0;
}

ZEST_SUITE(async_runtime_when_cancel, zest::LoopFixture) {

ZEST_CASE(all_child_cancel_cancels_the_rest) {
    event gate;
    event go;
    auto slow = [&]() -> task<int> {
        co_await gate.wait();
        co_return 2;
    };
    auto canceler = [&]() -> task<int> {
        co_await go.wait();
        co_await cancel();
        co_return 1;
    };
    auto combined = [&]() -> task<> {
        co_await when_all(slow(), canceler());
    };
    auto driver = [&]() -> task<> {
        go.set();
        co_return;
    };

    auto [result, drove] = run(combined(), driver());
    EXPECT(result.is_cancelled());
    EXPECT(!gate.has_waiters());
}

ZEST_CASE(all_child_cancelling_while_armed_starts_no_later_child) {
    int started = 0;
    auto later = [&]() -> task<int> {
        started += 1;
        co_return 1;
    };
    auto combined = [&]() -> task<> {
        co_await when_all(cancelled_value(), later());
    };

    auto [result] = run(combined());
    EXPECT(result.is_cancelled());
    EXPECT(started == 0);
}

// A child cancelled before the combinator starts it never runs: it ends
// cancelled at once, which cancels the combinator as a child's own
// cancellation does.
ZEST_CASE(child_cancelled_before_it_started_never_runs) {
    int ran = 0;
    auto child = [&]() -> task<int> {
        ran += 1;
        co_return 1;
    };
    auto all = [&]() -> task<> {
        auto cancelled = child();
        cancelled.cancel();
        co_await when_all(child(), std::move(cancelled));
    };
    auto any = [&]() -> task<> {
        auto cancelled = child();
        cancelled.cancel();
        co_await when_any(std::move(cancelled), child());
    };

    auto [all_result, any_result] = run(all(), any());
    EXPECT(all_result.is_cancelled());
    EXPECT(any_result.is_cancelled());
    // Only the child of when_all started ahead of the cancelled one ran.
    EXPECT(ran == 1);
}

// The first decision cancels every child not started yet, those behind a
// child cancelled before it started included.
ZEST_CASE(cancel_reaches_children_behind_one_cancelled_before_it_started) {
    event gate;
    auto ready = []() -> task<int> {
        co_return 1;
    };
    auto slow = [&]() -> task<int> {
        co_await gate.wait();
        co_return 2;
    };
    auto combined = [&]() -> task<std::size_t> {
        auto cancelled = ready();
        cancelled.cancel();
        auto winner = co_await when_any(ready(), std::move(cancelled), slow());
        co_return winner.index();
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(*result == 0U);
    EXPECT(!gate.has_waiters());
}

ZEST_CASE(any_child_cancel_cancels_the_rest) {
    event gate;
    event go;
    auto slow = [&]() -> task<int> {
        co_await gate.wait();
        co_return 2;
    };
    auto canceler = [&]() -> task<int> {
        co_await go.wait();
        co_await cancel();
        co_return 1;
    };
    auto combined = [&]() -> task<> {
        co_await when_any(slow(), canceler());
    };
    auto driver = [&]() -> task<> {
        go.set();
        co_return;
    };

    auto [result, drove] = run(combined(), driver());
    EXPECT(result.is_cancelled());
    EXPECT(!gate.has_waiters());
}

ZEST_CASE(any_child_cancelling_while_armed_starts_no_later_child) {
    int started = 0;
    auto later = [&]() -> task<int> {
        started += 1;
        co_return 1;
    };
    auto combined = [&]() -> task<> {
        co_await when_any(cancelled_value(), later());
    };

    auto [result] = run(combined());
    EXPECT(result.is_cancelled());
    EXPECT(started == 0);
}

ZEST_CASE(all_reports_an_intercepted_cancel) {
    event gate;
    auto slow = [&]() -> task<int> {
        co_await gate.wait();
        co_return 2;
    };
    auto combined = [&]() -> task<bool> {
        auto result = co_await when_all(slow(), cancelled_value().catch_cancel());
        co_return result.is_cancelled();
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(*result);
    EXPECT(!gate.has_waiters());
}

ZEST_CASE(any_reports_an_intercepted_cancel) {
    event gate;
    auto slow = [&]() -> task<int> {
        co_await gate.wait();
        co_return 2;
    };
    auto combined = [&]() -> task<bool> {
        auto result = co_await when_any(slow(), cancelled_value().catch_cancel());
        co_return result.is_cancelled();
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(*result);
    EXPECT(!gate.has_waiters());
}

ZEST_CASE(cancel_handled_inside_a_child_is_a_value) {
    auto handled = []() -> task<int> {
        auto result = co_await cancelled_value().catch_cancel();
        co_return result.is_cancelled() ? -1 : *result;
    };
    auto normal = []() -> task<int> {
        co_return 42;
    };
    auto combined = [&]() -> task<std::tuple<int, int>> {
        co_return co_await when_all(normal(), handled());
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(*result == std::tuple{42, -1});
}

ZEST_CASE(all_external_cancel_reaches_every_child) {
    event gates[2];
    auto child = [&](int id) -> task<int> {
        co_await gates[id].wait();
        co_return id;
    };
    auto combined = [&]() -> task<> {
        co_await when_all(child(0), child(1));
    };
    auto target = combined();
    auto cancel_it = [&]() -> task<> {
        target.cancel();
        co_return;
    };

    auto [result, driver] = run(target, cancel_it());
    EXPECT(result.is_cancelled());
    EXPECT(!gates[0].has_waiters());
    EXPECT(!gates[1].has_waiters());
}

ZEST_CASE(any_external_cancel_reaches_every_child) {
    event gates[2];
    auto child = [&](int id) -> task<int> {
        co_await gates[id].wait();
        co_return id;
    };
    auto combined = [&]() -> task<> {
        co_await when_any(child(0), child(1));
    };
    auto target = combined();
    auto cancel_it = [&]() -> task<> {
        target.cancel();
        co_return;
    };

    auto [result, driver] = run(target, cancel_it());
    EXPECT(result.is_cancelled());
    EXPECT(!gates[0].has_waiters());
    EXPECT(!gates[1].has_waiters());
}

// The range overloads report a child's own cancellation the same way.
ZEST_CASE(range_child_cancel_is_reported) {
    event gate;
    auto slow = [&]() -> task<int, void, cancellation> {
        co_await gate.wait();
        co_return 2;
    };
    auto cancelling = []() -> task<int, void, cancellation> {
        co_await yield();
        co_await cancel();
        co_return 1;
    };
    auto all = [&]() -> task<bool> {
        std::vector<task<int, void, cancellation>> tasks;
        tasks.push_back(slow());
        tasks.push_back(cancelling());
        co_return (co_await when_all(std::move(tasks))).is_cancelled();
    };
    auto any = [&]() -> task<bool> {
        std::vector<task<int, void, cancellation>> tasks;
        tasks.push_back(slow());
        tasks.push_back(cancelling());
        co_return (co_await when_any(std::move(tasks))).is_cancelled();
    };

    auto [all_cancelled, any_cancelled] = run(all(), any());
    ASSERT(all_cancelled.has_value());
    EXPECT(*all_cancelled);
    ASSERT(any_cancelled.has_value());
    EXPECT(*any_cancelled);
    EXPECT(!gate.has_waiters());
}

/// What a combinator's parent sees once the combinator returns.
struct Returned {
    bool cancelled = false;
    /// Child frames still alive, counted through a shared_ptr each holds.
    long frames_alive = 0;
};

// Structured completion: the combinator returns only once every cancelled
// child has finished, however long its cancellation takes, and has
// destroyed their frames by then.
ZEST_CASE(all_waits_for_cancelled_children_to_finish) {
    test::PendingOp op;
    auto frame = std::make_shared<int>();
    bool combined_done = false;
    auto slow = [&](std::shared_ptr<int>) -> task<int> {
        co_await op;
        co_return 2;
    };
    auto all = [&]() -> task<> {
        co_await when_all(slow(frame), cancelled_value());
    };
    auto combined = [&]() -> task<Returned> {
        auto result = co_await all().catch_cancel();
        combined_done = true;
        co_return Returned{.cancelled = result.is_cancelled(),
                           .frames_alive = frame.use_count() - 1};
    };
    auto finisher = [&]() -> task<bool> {
        bool done_before = combined_done;
        op.complete();
        co_return done_before;
    };

    auto [result, done_before] = run(combined(), finisher());
    ASSERT(result.has_value());
    EXPECT(result->cancelled);
    EXPECT(result->frames_alive == 0);
    EXPECT(op.cancel_requested());
    ASSERT(done_before.has_value());
    EXPECT(!*done_before);
    EXPECT(combined_done);
}

ZEST_CASE(any_waits_for_cancelled_children_to_finish) {
    test::PendingOp op;
    auto frame = std::make_shared<int>();
    bool combined_done = false;
    auto slow = [&](std::shared_ptr<int>) -> task<int> {
        co_await op;
        co_return 2;
    };
    auto fast = []() -> task<int> {
        co_return 1;
    };
    auto combined = [&]() -> task<std::pair<std::size_t, long>> {
        auto winner = co_await when_any(slow(frame), fast());
        combined_done = true;
        co_return std::pair{winner.index(), frame.use_count() - 1};
    };
    auto finisher = [&]() -> task<bool> {
        bool done_before = combined_done;
        op.complete();
        co_return done_before;
    };

    auto [result, done_before] = run(combined(), finisher());
    ASSERT(result.has_value());
    // The fast child won, and the slow one's frame is gone.
    EXPECT(*result == std::pair<std::size_t, long>{1, 0});
    EXPECT(op.cancel_requested());
    ASSERT(done_before.has_value());
    EXPECT(!*done_before);
}

// Awaiting a combinator under a cancelled task starts none of its children.
ZEST_CASE(checkpoint_starts_no_child) {
    int started = 0;
    task<> target;
    auto child = [&]() -> task<> {
        started += 1;
        co_return;
    };
    auto worker = [&]() -> task<> {
        target.cancel();
        co_await when_all(child(), child());
    };
    target = worker();

    auto [result] = run(target);
    EXPECT(result.is_cancelled());
    EXPECT(started == 0);
}

};  // ZEST_SUITE(async_runtime_when_cancel)

}  // namespace

}  // namespace kota
