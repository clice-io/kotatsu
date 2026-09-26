// ZEST_SUITE(task_group_lifetime): frame lifetime and settled-group behavior —
// spawn after join/cancel is rejected, not-awaited groups still run children,
// completed frames reclaimed eagerly (tombstone compaction), and structured
// completion waiting for cancelled children's frames. Spawn/join basics in
// basics.cpp; cancel semantics in cancel.cpp; errors in errors.cpp.
#include "../../loop_fixture.h"
#include "../../support.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

ZEST_SUITE(task_group_lifetime, loop_fixture) {

ZEST_CASE(spawn_after_join) {
    int count = 0;

    auto work = [&]() -> task<> {
        count += 1;
        co_return;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(work());
        co_await group.join();
        group.spawn(work());
    };

    auto t = driver();
    schedule_all(t);
    EXPECT(t->is_finished());
    EXPECT(count == 1);
}

ZEST_CASE(not_awaited) {
    int count = 0;

    auto work = [&]() -> task<> {
        count += 1;
        co_return;
    };

    {
        task_group<> group(loop);
        group.spawn(work());
        group.spawn(work());
    }

    EXPECT(count == 2);
}

ZEST_CASE(destroy_mixed_completed_and_pending) {
    int op_destroyed = 0;
    int sync_count = 0;

    auto sync_work = [&]() -> task<> {
        sync_count += 1;
        co_return;
    };

    auto pending_work = [&]() -> task<> {
        deferred_cancel_await op(op_destroyed);
        co_await op;
    };

    auto finisher = []() -> task<> {
        co_await sleep(1);
        deferred_cancel_await::finish_pending_cancel();
    };

    auto runner = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(sync_work());
        group.spawn(sync_work());
        group.spawn(pending_work());
        group.cancel();
        co_await group.join();
    };

    auto runner_task = runner();
    auto finisher_task = finisher();
    schedule_all(runner_task, finisher_task);

    EXPECT(sync_count == 2);
    EXPECT(op_destroyed == 1);
}

ZEST_CASE(group_waits_for_cancelled_children) {
    int op_destroyed = 0;

    auto slow = [&]() -> task<> {
        deferred_cancel_await op(op_destroyed);
        co_await op;
    };

    auto fast_fail = [&]() -> task<int, error> {
        co_await fail(error::connection_refused);
    };

    auto finisher = []() -> task<> {
        co_await sleep(1);
        deferred_cancel_await::finish_pending_cancel();
    };

    auto runner = [&]() -> task<> {
        task_group<error> group(loop);
        group.spawn(slow());
        group.spawn(fast_fail());
        auto result = co_await group.join();
        EXPECT(result.has_error());
    };

    auto runner_task = runner();
    auto finisher_task = finisher();
    schedule_all(runner_task, finisher_task);
    EXPECT(op_destroyed == 1);
}

// spawn() returns bool indicating acceptance
ZEST_CASE(spawn_returns_false_after_settled) {
    bool accepted = true;

    auto work = [&]() -> task<> {
        co_return;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        EXPECT(group.spawn(work()));
        co_await group.join();
        accepted = group.spawn(work());
    };

    auto t = driver();
    schedule_all(t);
    EXPECT(!accepted);
}

ZEST_CASE(spawn_returns_false_after_cancel) {
    auto work = [&]() -> task<> {
        co_return;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        EXPECT(group.spawn(work()));
        group.cancel();
        EXPECT(!group.spawn(work()));
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
}

// Completed children are reclaimed eagerly (frame destroyed as soon as the
// child finishes) instead of accumulating until the group is destroyed;
// failed children stay alive so join() can extract their errors. Also
// exercises tombstone compaction: the third spawn triggers it, and the
// error handler must stay paired with the failing child across the shift.
ZEST_CASE(reclaims_completed_child_frames) {
    int destroyed = 0;

    struct probe {
        int* counter = nullptr;

        explicit probe(int* c) : counter(c) {}

        probe(probe&& other) noexcept : counter(std::exchange(other.counter, nullptr)) {}

        ~probe() {
            if(counter) {
                *counter += 1;
            }
        }
    };

    auto work = [](probe) -> task<> {
        co_return;
    };

    auto failing = [](probe) -> task<void, error> {
        co_await fail(error::connection_refused);
    };

    auto driver = [&]() -> task<> {
        task_group<error> group(loop);
        group.spawn(work(probe(&destroyed)));
        group.spawn(work(probe(&destroyed)));
        EXPECT(destroyed == 2);

        group.spawn(failing(probe(&destroyed)));
        EXPECT(destroyed == 2);

        auto res = co_await group.join();
        EXPECT(res.has_error());
        EXPECT(res.error().size() == 1u);
        EXPECT(res.error().front() == error::connection_refused);
        EXPECT(destroyed == 2);
    };

    auto t = driver();
    schedule_all(t);

    EXPECT(t->is_finished());
    // ~task_group (at the end of driver's body) destroyed the failed child.
    EXPECT(destroyed == 3);
}

};  // ZEST_SUITE(task_group_lifetime)

}  // namespace kota
