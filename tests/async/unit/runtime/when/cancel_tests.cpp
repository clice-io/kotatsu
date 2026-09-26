#include <tuple>
#include <variant>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "async/harness/pending_op.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

task<int> cancelled_value() {
    co_await cancel();
    co_return 0;
}

ZEST_SUITE(async_runtime_when_cancel, test::LoopFixture) {

ZEST_CASE(all_child_cancel_cancels_the_rest) {
    event gate;
    event go;
    bool slow_finished = false;
    auto slow = [&]() -> task<int> {
        co_await gate.wait();
        slow_finished = true;
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
    EXPECT(!slow_finished);
    EXPECT(gate.get_head() == nullptr);
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

ZEST_CASE(any_child_cancel_cancels_the_rest) {
    event gate;
    event go;
    bool slow_finished = false;
    auto slow = [&]() -> task<int> {
        co_await gate.wait();
        slow_finished = true;
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
    EXPECT(!slow_finished);
    EXPECT(gate.get_head() == nullptr);
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
    bool slow_finished = false;
    auto slow = [&]() -> task<int> {
        co_await gate.wait();
        slow_finished = true;
        co_return 2;
    };
    auto combined = [&]() -> task<bool> {
        auto result = co_await when_all(slow(), cancelled_value().catch_cancel());
        co_return result.is_cancelled();
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(*result);
    EXPECT(!slow_finished);
}

ZEST_CASE(any_reports_an_intercepted_cancel) {
    event gate;
    bool slow_finished = false;
    auto slow = [&]() -> task<int> {
        co_await gate.wait();
        slow_finished = true;
        co_return 2;
    };
    auto combined = [&]() -> task<bool> {
        auto result = co_await when_any(slow(), cancelled_value().catch_cancel());
        co_return result.is_cancelled();
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(*result);
    EXPECT(!slow_finished);
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
    int finished = 0;
    auto child = [&](int id) -> task<int> {
        co_await gates[id].wait();
        finished += 1;
        co_return id;
    };
    auto combined = [&]() -> task<> {
        co_await when_all(child(0), child(1));
    };
    auto target = combined();
    auto* node = target.operator->();
    auto cancel_it = [&]() -> task<> {
        node->cancel();
        co_return;
    };

    auto [result, driver] = run(std::move(target), cancel_it());
    EXPECT(result.is_cancelled());
    EXPECT(finished == 0);
    EXPECT(gates[0].get_head() == nullptr);
    EXPECT(gates[1].get_head() == nullptr);
}

ZEST_CASE(any_external_cancel_reaches_every_child) {
    event gates[2];
    int finished = 0;
    auto child = [&](int id) -> task<int> {
        co_await gates[id].wait();
        finished += 1;
        co_return id;
    };
    auto combined = [&]() -> task<> {
        co_await when_any(child(0), child(1));
    };
    auto target = combined();
    auto* node = target.operator->();
    auto cancel_it = [&]() -> task<> {
        node->cancel();
        co_return;
    };

    auto [result, driver] = run(std::move(target), cancel_it());
    EXPECT(result.is_cancelled());
    EXPECT(finished == 0);
    EXPECT(gates[0].get_head() == nullptr);
    EXPECT(gates[1].get_head() == nullptr);
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
    EXPECT(gate.get_head() == nullptr);
}

// Structured completion: the combinator returns only once every cancelled
// child has finished, however long its cancellation takes.
ZEST_CASE(all_waits_for_cancelled_children_to_finish) {
    test::PendingOp op;
    bool combined_done = false;
    auto slow = [&]() -> task<int> {
        co_await op;
        co_return 2;
    };
    auto all = [&]() -> task<> {
        co_await when_all(slow(), cancelled_value());
    };
    auto combined = [&]() -> task<> {
        co_await all().catch_cancel();
        combined_done = true;
    };
    auto finisher = [&]() -> task<bool> {
        bool done_before = combined_done;
        op.complete();
        co_return done_before;
    };

    auto [result, done_before] = run(combined(), finisher());
    EXPECT(result.has_value());
    EXPECT(op.is_cancelled());
    ASSERT(done_before.has_value());
    EXPECT(!*done_before);
    EXPECT(combined_done);
}

ZEST_CASE(any_waits_for_cancelled_children_to_finish) {
    test::PendingOp op;
    bool combined_done = false;
    auto slow = [&]() -> task<int> {
        co_await op;
        co_return 2;
    };
    auto fast = []() -> task<int> {
        co_return 1;
    };
    auto combined = [&]() -> task<std::variant<int, int>> {
        auto winner = co_await when_any(slow(), fast());
        combined_done = true;
        co_return winner;
    };
    auto finisher = [&]() -> task<bool> {
        bool done_before = combined_done;
        op.complete();
        co_return done_before;
    };

    auto [result, done_before] = run(combined(), finisher());
    ASSERT(result.has_value());
    EXPECT(result->index() == 1U);
    EXPECT(op.is_cancelled());
    ASSERT(done_before.has_value());
    EXPECT(!*done_before);
}

// Awaiting a combinator under a cancelled task starts none of its children.
ZEST_CASE(checkpoint_starts_no_child) {
    int started = 0;
    async_node* self = nullptr;
    auto child = [&]() -> task<> {
        started += 1;
        co_return;
    };
    auto worker = [&]() -> task<> {
        self->cancel();
        co_await when_all(child(), child());
    };
    auto target = worker();
    self = target.operator->();

    auto [result] = run(std::move(target));
    EXPECT(result.is_cancelled());
    EXPECT(started == 0);
}

};  // ZEST_SUITE(async_runtime_when_cancel)

}  // namespace

}  // namespace kota
