#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "async/harness/pending_op.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

struct CustomError {
    int code = 0;
};

template <typename Group, typename Task>
concept spawnable = requires(Group& group, Task task) { group.spawn(std::move(task)); };

ZEST_SUITE(async_runtime_task_group_lifetime, zest::LoopFixture) {

ZEST_CASE(spawn_accepts_the_declared_error_types_only) {
    STATIC_EXPECT(spawnable<task_group<>, task<>>);
    STATIC_EXPECT(!spawnable<task_group<>, task<int, error>>);
    STATIC_EXPECT(spawnable<task_group<error>, task<int, error>>);
    STATIC_EXPECT(spawnable<task_group<error>, task<>>);
    STATIC_EXPECT(!spawnable<task_group<error>, task<int, CustomError>>);
    STATIC_EXPECT(spawnable<task_group<error, CustomError>, task<int, CustomError>>);
}

ZEST_CASE(spawn_after_join_fails) {
    int started = 0;
    auto work = [&]() -> task<> {
        started += 1;
        co_return;
    };
    auto driver = [&]() -> task<std::vector<bool>> {
        task_group<> group;
        std::vector<bool> accepted{group.spawn(work())};
        co_await group.join();
        accepted.push_back(group.spawn(work()));
        co_return accepted;
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(*result == std::vector{true, false});
    EXPECT(started == 1);
}

ZEST_CASE(spawn_after_cancel_fails) {
    int started = 0;
    auto work = [&]() -> task<> {
        started += 1;
        co_return;
    };
    auto driver = [&]() -> task<std::vector<bool>> {
        task_group<> group;
        std::vector<bool> accepted{group.spawn(work())};
        group.cancel();
        accepted.push_back(group.spawn(work()));
        co_await group.join();
        co_return accepted;
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(*result == std::vector{true, false});
    EXPECT(started == 1);
}

// A child that fails cancels its siblings, and the group takes no child after
// that.
ZEST_CASE(spawn_after_a_child_failed_fails) {
    int started = 0;
    auto work = [&]() -> task<void, error> {
        started += 1;
        co_return;
    };
    auto failing = []() -> task<void, error> {
        co_await fail(error::connection_refused);
    };
    auto driver = [&]() -> task<bool> {
        task_group<error> group;
        group.spawn(failing());
        bool accepted = group.spawn(work());
        [[maybe_unused]] auto joined = co_await group.join();
        co_return accepted;
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(!*result);
    EXPECT(started == 0);
}

// A child that ended cancelled leaves the group open.
ZEST_CASE(spawn_after_a_child_ended_cancelled_starts_the_child) {
    int started = 0;
    auto work = [&]() -> task<> {
        started += 1;
        co_return;
    };
    auto cancelling = []() -> task<> {
        co_await cancel();
    };
    auto driver = [&]() -> task<bool> {
        task_group<> group;
        group.spawn(cancelling());
        bool accepted = group.spawn(work());
        co_await group.join();
        co_return accepted;
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(*result);
    EXPECT(started == 1);
}

// A group whose children all finished while being spawned may go without a
// join().
ZEST_CASE(group_of_finished_children_needs_no_join) {
    int finished = 0;
    auto work = [&]() -> task<> {
        finished += 1;
        co_return;
    };

    {
        task_group<> group;
        group.spawn(work());
        group.spawn(work());
    }

    EXPECT(finished == 2);
}

// Destroying a group whose children still run lets them go: each is
// cancelled, and its frame goes once it has ended.
ZEST_CASE(destroying_the_group_cancels_its_running_children) {
    event gate;
    auto frames = std::make_shared<int>();
    bool resumed = false;
    auto waiting = [&](std::shared_ptr<int>) -> task<> {
        co_await gate.wait();
        resumed = true;
    };
    auto driver = [&]() -> task<> {
        {
            task_group<> group;
            group.spawn(waiting(frames));
        }
        co_return;
    };

    auto [result] = run(driver());
    EXPECT(result.has_value());
    EXPECT(!resumed);
    EXPECT(!gate.has_waiters());
    EXPECT(frames.use_count() == 1);
}

// The destructor's cancel resumes a child that catches it, which runs on
// inside the destructor to its next suspending co_await; the group refuses
// what that child spawns meanwhile.
ZEST_CASE(child_the_destructor_resumes_cannot_spawn) {
    event gate;
    task_group<>* dying = nullptr;
    std::optional<bool> spawned;
    auto waiting = [&]() -> task<> {
        co_await gate.wait();
    };
    auto sibling = []() -> task<> {
        co_return;
    };
    auto child = [&]() -> task<> {
        co_await waiting().catch_cancel();
        spawned = dying->spawn(sibling());
    };
    auto driver = [&]() -> task<> {
        {
            task_group<> group;
            dying = &group;
            group.spawn(child());
        }
        co_return;
    };

    auto [result] = run(driver());
    EXPECT(result.has_value());
    ASSERT(spawned.has_value());
    EXPECT(!*spawned);
}

// A child let go keeps its frame until its cancellation completes, however
// long after the group is gone that is.
ZEST_CASE(destroyed_group_child_is_freed_once_its_cancel_completes) {
    test::PendingOp op;
    auto frames = std::make_shared<int>();
    auto pending = [&](std::shared_ptr<int>) -> task<void, error> {
        co_await op;
    };
    auto driver = [&]() -> task<long> {
        {
            task_group<error> group;
            group.spawn(pending(frames));
        }
        co_return frames.use_count() - 1;
    };
    auto finisher = [&]() -> task<> {
        op.complete();
        co_return;
    };

    auto [alive, finished] = run(driver(), finisher());
    ASSERT(alive.has_value());
    EXPECT(*alive == 1);
    EXPECT(op.cancel_requested());
    EXPECT(frames.use_count() == 1);
}

// Every child's frame goes as soon as the child ends, a failed one too, whose
// error the group has taken by then. Each frame holds a copy of `frames`, so
// its use count tells how many are alive.
ZEST_CASE(finished_children_are_reclaimed_at_once) {
    auto frames = std::make_shared<int>();
    auto work = [](std::shared_ptr<int>) -> task<> {
        co_return;
    };
    auto failing = [](std::shared_ptr<int>) -> task<void, error> {
        co_await fail(error::connection_refused);
    };

    struct Seen {
        long alive_after_two = -1;
        long alive_after_failing = -1;
        std::vector<error> errors;
    };

    auto driver = [&]() -> task<Seen> {
        Seen seen;
        task_group<error> group;
        group.spawn(work(frames));
        group.spawn(work(frames));
        seen.alive_after_two = frames.use_count() - 1;
        group.spawn(failing(frames));
        seen.alive_after_failing = frames.use_count() - 1;
        auto joined = co_await group.join();
        if(joined.has_error()) {
            seen.errors = std::move(joined).error();
        }
        co_return seen;
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(result->alive_after_two == 0);
    EXPECT(result->alive_after_failing == 0);
    EXPECT(result->errors == std::vector{error::connection_refused});
}

// A child cancelled before it is spawned never runs: it ends cancelled at
// once, and its siblings run on.
ZEST_CASE(child_cancelled_before_spawn_never_runs) {
    event gate;
    bool ran = false;
    bool slow_finished = false;
    auto slow = [&]() -> task<> {
        co_await gate.wait();
        slow_finished = true;
    };
    auto work = [&]() -> task<> {
        ran = true;
        co_return;
    };
    auto driver = [&]() -> task<bool> {
        task_group<> group;
        group.spawn(slow());
        auto cancelled = work();
        cancelled.cancel();
        bool accepted = group.spawn(std::move(cancelled));
        gate.set();
        co_await group.join();
        co_return accepted;
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(*result);
    EXPECT(!ran);
    // Its sibling ran on.
    EXPECT(slow_finished);
}

// Structured completion: join() returns only once every cancelled child has
// finished, however long its cancellation takes; the group frees the frames.
ZEST_CASE(join_after_cancel_waits_for_pending_children) {
    test::PendingOp op;
    auto frames = std::make_shared<int>();
    int finished = 0;
    bool joined = false;
    auto at_once = [&]() -> task<> {
        finished += 1;
        co_return;
    };
    auto pending = [&](std::shared_ptr<int>) -> task<> {
        co_await op;
    };
    auto driver = [&]() -> task<> {
        task_group<> group;
        group.spawn(at_once());
        group.spawn(pending(frames));
        group.cancel();
        co_await group.join();
        joined = true;
    };
    auto finisher = [&]() -> task<bool> {
        bool joined_before = joined;
        op.complete();
        co_return joined_before;
    };

    auto [result, joined_before] = run(driver(), finisher());
    EXPECT(result.has_value());
    EXPECT(op.cancel_requested());
    ASSERT(joined_before.has_value());
    EXPECT(!*joined_before);
    EXPECT(joined);
    EXPECT(finished == 1);
    EXPECT(frames.use_count() == 1);
}

ZEST_CASE(join_after_an_error_waits_for_pending_children) {
    test::PendingOp op;
    auto frames = std::make_shared<int>();
    bool joined = false;
    auto pending = [&](std::shared_ptr<int>) -> task<> {
        co_await op;
    };
    auto failing = []() -> task<int, error> {
        co_await fail(error::connection_refused);
    };
    auto driver = [&]() -> task<std::vector<error>> {
        task_group<error> group;
        group.spawn(pending(frames));
        group.spawn(failing());
        auto result = co_await group.join();
        joined = true;
        if(result.has_error()) {
            co_return std::move(result).error();
        }
        co_return std::vector<error>{};
    };
    auto finisher = [&]() -> task<bool> {
        bool joined_before = joined;
        op.complete();
        co_return joined_before;
    };

    auto [result, joined_before] = run(driver(), finisher());
    ASSERT(result.has_value());
    EXPECT(*result == std::vector{error::connection_refused});
    EXPECT(op.cancel_requested());
    ASSERT(joined_before.has_value());
    EXPECT(!*joined_before);
    EXPECT(frames.use_count() == 1);
}

};  // ZEST_SUITE(async_runtime_task_group_lifetime)

}  // namespace

}  // namespace kota
