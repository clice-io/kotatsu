#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_runtime_task_group_cancel, test::LoopFixture) {

// A child's own cancellation cancels its siblings but is no failure: join()
// returns normally.
ZEST_CASE(child_cancel_cancels_the_siblings) {
    event gate;
    bool slow_finished = false;
    auto slow = [&]() -> task<> {
        co_await gate.wait();
        slow_finished = true;
    };
    auto canceler = []() -> task<> {
        co_await yield();
        co_await cancel();
    };
    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(slow());
        group.spawn(canceler());
        co_await group.join();
    };

    auto [result] = run(driver());
    EXPECT(result.has_value());
    EXPECT(!slow_finished);
    EXPECT(gate.get_head() == nullptr);
}

ZEST_CASE(cancel_before_join_cancels_every_child) {
    event gate;
    int finished = 0;
    auto slow = [&]() -> task<> {
        co_await gate.wait();
        finished += 1;
    };
    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(slow());
        group.spawn(slow());
        group.cancel();
        group.cancel();
        co_await group.join();
    };

    auto [result] = run(driver());
    EXPECT(result.has_value());
    EXPECT(finished == 0);
    EXPECT(gate.get_head() == nullptr);
}

ZEST_CASE(cancel_of_an_empty_group_lets_join_complete) {
    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.cancel();
        co_await group.join();
    };

    auto [result] = run(driver());
    EXPECT(result.has_value());
}

ZEST_CASE(cancel_while_join_waits_cancels_the_rest) {
    event fast_gate;
    event slow_gate;
    int fast_finished = 0;
    int slow_finished = 0;
    task_group<>* group_ptr = nullptr;
    auto fast = [&]() -> task<> {
        co_await fast_gate.wait();
        fast_finished += 1;
    };
    auto slow = [&]() -> task<> {
        co_await slow_gate.wait();
        slow_finished += 1;
    };
    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group_ptr = &group;
        group.spawn(fast());
        group.spawn(slow());
        co_await group.join();
    };
    auto canceler = [&]() -> task<> {
        fast_gate.set();
        co_await yield();
        group_ptr->cancel();
    };

    auto [result, drove] = run(driver(), canceler());
    EXPECT(result.has_value());
    EXPECT(fast_finished == 1);
    EXPECT(slow_finished == 0);
}

ZEST_CASE(child_can_cancel_its_group_after_suspending) {
    event gate;
    bool slow_finished = false;
    task_group<>* group_ptr = nullptr;
    auto slow = [&]() -> task<> {
        co_await gate.wait();
        slow_finished = true;
    };
    auto canceler = [&]() -> task<> {
        co_await yield();
        group_ptr->cancel();
    };
    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group_ptr = &group;
        group.spawn(canceler());
        group.spawn(slow());
        co_await group.join();
    };

    auto [result] = run(driver());
    EXPECT(result.has_value());
    EXPECT(!slow_finished);
}

// spawn() runs a child until it first suspends; one that cancels its group
// before that goes on to that point instead of being finalized under its own
// feet, and join() resumes only once it has.
ZEST_CASE(child_cancelling_its_group_runs_to_its_first_suspension) {
    event gate;
    bool canceler_finished = false;
    task_group<>* group_ptr = nullptr;
    auto slow = [&]() -> task<> {
        co_await gate.wait();
    };
    auto canceler = [&]() -> task<> {
        group_ptr->cancel();
        canceler_finished = true;
        co_return;
    };
    auto driver = [&]() -> task<bool> {
        task_group<> group(loop);
        group_ptr = &group;
        group.spawn(slow());
        co_await group.join();
        co_return canceler_finished;
    };
    auto spawner = [&]() -> task<> {
        group_ptr->spawn(canceler());
        co_return;
    };

    auto [result, drove] = run(driver(), spawner());
    ASSERT(result.has_value());
    EXPECT(*result);
}

ZEST_CASE(joiner_cancel_cancels_the_children) {
    event gate;
    int finished = 0;
    auto slow = [&]() -> task<> {
        co_await gate.wait();
        finished += 1;
    };
    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(slow());
        group.spawn(slow());
        co_await group.join();
    };
    auto target = driver();
    auto* node = target.operator->();
    auto cancel_it = [&]() -> task<> {
        node->cancel();
        co_return;
    };

    auto [result, drove] = run(std::move(target), cancel_it());
    EXPECT(result.is_cancelled());
    EXPECT(finished == 0);
    EXPECT(gate.get_head() == nullptr);
}

// join() under a cancelled task cancels the children and waits for them
// before the cancellation goes on.
ZEST_CASE(checkpoint_join_cancels_the_group) {
    event gate;
    bool slow_finished = false;
    async_node* self = nullptr;
    auto slow = [&]() -> task<> {
        co_await gate.wait();
        slow_finished = true;
    };
    auto worker = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(slow());
        self->cancel();
        co_await group.join();
    };
    auto target = worker();
    self = target.operator->();

    auto [result] = run(std::move(target));
    EXPECT(result.is_cancelled());
    EXPECT(!slow_finished);
    EXPECT(gate.get_head() == nullptr);
}

};  // ZEST_SUITE(async_runtime_task_group_cancel)

}  // namespace

}  // namespace kota
