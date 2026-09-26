#include <memory>
#include <utility>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "async/harness/pending_op.h"
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

ZEST_SUITE(async_runtime_task_group_lifetime, test::LoopFixture) {

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
        task_group<> group(loop);
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
        task_group<> group(loop);
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

// A group whose children all finished while being spawned may go without a
// join().
ZEST_CASE(group_of_finished_children_needs_no_join) {
    int finished = 0;
    auto work = [&]() -> task<> {
        finished += 1;
        co_return;
    };

    {
        task_group<> group(loop);
        group.spawn(work());
        group.spawn(work());
    }

    EXPECT(finished == 2);
}

// A finished child's frame goes at once instead of waiting for the group; a
// failed child stays until join() has taken its error. The third spawn also
// compacts the slots of the reclaimed children, and its error must still be
// matched to it. Each frame holds a copy of `frames`, so its use count tells
// how many are alive.
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
        long alive_after_join = -1;
    };

    auto driver = [&]() -> task<Seen> {
        Seen seen;
        task_group<error> group(loop);
        group.spawn(work(frames));
        group.spawn(work(frames));
        seen.alive_after_two = frames.use_count() - 1;
        group.spawn(failing(frames));
        seen.alive_after_failing = frames.use_count() - 1;
        auto joined = co_await group.join();
        if(joined.has_error()) {
            seen.errors = std::move(joined).error();
        }
        seen.alive_after_join = frames.use_count() - 1;
        co_return seen;
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(result->alive_after_two == 0);
    EXPECT(result->alive_after_failing == 1);
    EXPECT(result->errors == std::vector{error::connection_refused});
    EXPECT(result->alive_after_join == 1);
    // The group's destructor released the failed child.
    EXPECT(frames.use_count() == 1);
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
        task_group<> group(loop);
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
    EXPECT(op.is_cancelled());
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
        task_group<error> group(loop);
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
    EXPECT(op.is_cancelled());
    ASSERT(joined_before.has_value());
    EXPECT(!*joined_before);
    EXPECT(frames.use_count() == 1);
}

};  // ZEST_SUITE(async_runtime_task_group_lifetime)

}  // namespace

}  // namespace kota
