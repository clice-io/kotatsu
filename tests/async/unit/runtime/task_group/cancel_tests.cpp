#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_runtime_task_group_cancel, zest::LoopFixture) {

// A child that ends cancelled just ends: its siblings run on, and join()
// returns once they have.
ZEST_CASE(child_cancel_leaves_the_siblings_running) {
    event gate;
    event cancelling;
    bool slow_finished = false;
    auto slow = [&]() -> task<> {
        co_await gate.wait();
        slow_finished = true;
    };
    auto canceler = [&]() -> task<> {
        co_await yield();
        // The opener wakes once this child has ended.
        cancelling.set();
        co_await cancel();
    };
    auto driver = [&]() -> task<> {
        task_group<> group;
        group.spawn(slow());
        group.spawn(canceler());
        co_await group.join();
    };
    auto opener = [&]() -> task<> {
        co_await cancelling.wait();
        gate.set();
    };

    auto [result, opened] = run(driver(), opener());
    ZEXPECT(result.has_value());
    ZEXPECT(opened.has_value());
    ZEXPECT(slow_finished);
}

ZEST_CASE(cancel_before_join_cancels_every_child) {
    event gate;
    auto slow = [&]() -> task<> {
        co_await gate.wait();
    };
    auto driver = [&]() -> task<> {
        task_group<> group;
        group.spawn(slow());
        group.spawn(slow());
        group.cancel();
        group.cancel();
        co_await group.join();
    };

    auto [result] = run(driver());
    ZEXPECT(result.has_value());
    ZEXPECT(!gate.has_waiters());
}

ZEST_CASE(cancel_of_an_empty_group_lets_join_complete) {
    auto driver = [&]() -> task<> {
        task_group<> group;
        group.cancel();
        co_await group.join();
    };

    auto [result] = run(driver());
    ZEXPECT(result.has_value());
}

ZEST_CASE(cancel_while_join_waits_cancels_the_rest) {
    event fast_gate;
    event slow_gate;
    int fast_finished = 0;
    task_group<>* group_ptr = nullptr;
    auto fast = [&]() -> task<> {
        co_await fast_gate.wait();
        fast_finished += 1;
    };
    auto slow = [&]() -> task<> {
        co_await slow_gate.wait();
    };
    auto driver = [&]() -> task<> {
        task_group<> group;
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
    ZEXPECT(result.has_value());
    ZEXPECT(fast_finished == 1);
    ZEXPECT(!slow_gate.has_waiters());
}

ZEST_CASE(child_can_cancel_its_group_after_suspending) {
    event gate;
    task_group<>* group_ptr = nullptr;
    auto slow = [&]() -> task<> {
        co_await gate.wait();
    };
    auto canceler = [&]() -> task<> {
        co_await yield();
        group_ptr->cancel();
    };
    auto driver = [&]() -> task<> {
        task_group<> group;
        group_ptr = &group;
        group.spawn(canceler());
        group.spawn(slow());
        co_await group.join();
    };

    auto [result] = run(driver());
    ZEXPECT(result.has_value());
    ZEXPECT(!gate.has_waiters());
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
        task_group<> group;
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
    ZASSERT(result.has_value());
    ZEXPECT(*result);
}

ZEST_CASE(joiner_cancel_cancels_the_children) {
    event gate;
    auto slow = [&]() -> task<> {
        co_await gate.wait();
    };
    auto driver = [&]() -> task<> {
        task_group<> group;
        group.spawn(slow());
        group.spawn(slow());
        co_await group.join();
    };
    auto target = driver();
    auto cancel_it = [&]() -> task<> {
        target.cancel();
        co_return;
    };

    auto [result, drove] = run(target, cancel_it());
    ZEXPECT(result.is_cancelled());
    ZEXPECT(!gate.has_waiters());
}

// join() under a cancelled task cancels the children and waits for them
// before the cancellation goes on.
ZEST_CASE(checkpoint_join_cancels_the_group) {
    event gate;
    task<> target;
    auto slow = [&]() -> task<> {
        co_await gate.wait();
    };
    auto worker = [&]() -> task<> {
        task_group<> group;
        group.spawn(slow());
        target.cancel();
        co_await group.join();
    };
    target = worker();

    auto [result] = run(target);
    ZEXPECT(result.is_cancelled());
    ZEXPECT(!gate.has_waiters());
}

};  // ZEST_SUITE(async_runtime_task_group_cancel)

}  // namespace

}  // namespace kota
