#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <vector>

#include "async/harness/pending_op.h"
#include "kotatsu/zest/async.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/support/config.h"
#include "kotatsu/async/async.h"

namespace kotatsu::async {

namespace {

struct CustomError {
    int code = 0;
};

template <typename Group, typename Task>
concept spawnable = requires(Group& group, Task task) { group.spawn(std::move(task)); };

/// What owns a group, as a connection's state owns the group of its
/// requests, kept alive by the children it spawns.
struct Session {
    task_group<> group;
    event never;
};

/// Storage that outlives the Session made in it: its deleter poisons the
/// bytes once the Session is destroyed, so that a write to the Session, its
/// group included, after it went shows as a byte that changed.
struct SessionStorage {
    constexpr static std::byte poison{0xA5};

    alignas(Session) std::array<std::byte, sizeof(Session)> bytes{};
    bool destroyed = false;

    std::shared_ptr<Session> make() {
        return std::shared_ptr<Session>(new (bytes.data()) Session(), [this](Session* session) {
            session->~Session();
            bytes.fill(poison);
            destroyed = true;
        });
    }

    /// Nothing wrote to the Session since it was destroyed.
    bool untouched() const {
        return std::ranges::all_of(bytes, [](std::byte b) { return b == poison; });
    }
};

/// A child that keeps `session` alive while it waits on it.
task<> waits_on(std::shared_ptr<Session> session) {
    co_await session->never.wait();
}

ZEST_SUITE(async_runtime_task_group_lifetime, zest::LoopFixture) {

ZEST_CASE(spawn_accepts_the_declared_error_types_only) {
    ZSTATIC_EXPECT(spawnable<task_group<>, task<>>);
    ZSTATIC_EXPECT(!spawnable<task_group<>, task<int, error>>);
    ZSTATIC_EXPECT(spawnable<task_group<error>, task<int, error>>);
    ZSTATIC_EXPECT(spawnable<task_group<error>, task<>>);
    ZSTATIC_EXPECT(!spawnable<task_group<error>, task<int, CustomError>>);
    ZSTATIC_EXPECT(spawnable<task_group<error, CustomError>, task<int, CustomError>>);
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
    ZASSERT(result.has_value());
    ZEXPECT(*result == std::vector{true, false});
    ZEXPECT(started == 1);
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
    ZASSERT(result.has_value());
    ZEXPECT(*result == std::vector{true, false});
    ZEXPECT(started == 1);
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
    ZASSERT(result.has_value());
    ZEXPECT(!*result);
    ZEXPECT(started == 0);
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
    ZASSERT(result.has_value());
    ZEXPECT(*result);
    ZEXPECT(started == 1);
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

    ZEXPECT(finished == 2);
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
    ZEXPECT(result.has_value());
    ZEXPECT(!resumed);
    ZEXPECT(!gate.has_waiters());
    ZEXPECT(frames.use_count() == 1);
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
    ZEXPECT(result.has_value());
    ZASSERT(spawned.has_value());
    ZEXPECT(!*spawned);
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
    ZASSERT(alive.has_value());
    ZEXPECT(*alive == 1);
    ZEXPECT(op.cancel_requested());
    ZEXPECT(frames.use_count() == 1);
}

// Every child's frame goes once the child has ended and whatever runs has
// suspended, a failed one too, whose error the group has taken by then. Each
// frame holds a copy of `frames`, so its use count tells how many are alive.
ZEST_CASE(finished_children_are_reclaimed_once_their_spawner_suspends) {
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
        co_await yield();
        seen.alive_after_two = frames.use_count() - 1;
        group.spawn(failing(frames));
        co_await yield();
        seen.alive_after_failing = frames.use_count() - 1;
        auto joined = co_await group.join();
        if(joined.has_error()) {
            seen.errors = std::move(joined).error();
        }
        co_return seen;
    };

    auto [result] = run(driver());
    ZASSERT(result.has_value());
    ZEXPECT(result->alive_after_two == 0);
    ZEXPECT(result->alive_after_failing == 0);
    ZEXPECT(result->errors == std::vector{error::connection_refused});
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
    ZASSERT(result.has_value());
    ZEXPECT(*result);
    ZEXPECT(!ran);
    // Its sibling ran on.
    ZEXPECT(slow_finished);
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
    ZEXPECT(result.has_value());
    ZEXPECT(op.cancel_requested());
    ZASSERT(joined_before.has_value());
    ZEXPECT(!*joined_before);
    ZEXPECT(joined);
    ZEXPECT(finished == 1);
    ZEXPECT(frames.use_count() == 1);
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
    ZASSERT(result.has_value());
    ZEXPECT(*result == std::vector{error::connection_refused});
    ZEXPECT(op.cancel_requested());
    ZASSERT(joined_before.has_value());
    ZEXPECT(!*joined_before);
    ZEXPECT(frames.use_count() == 1);
}

// A child may hold the last reference to what owns its group: the group is
// done with the child before the child's frame, and what that holds, goes.
ZEST_CASE(child_owning_its_group_frees_it_once_the_group_is_done) {
    SessionStorage storage;
    auto child = [](std::shared_ptr<Session>) -> task<> {
        co_await yield();
    };
    auto start = [&]() -> task<> {
        auto session = storage.make();
        session->group.spawn(child(session));
        co_return;
    };

    // By hand: run() would stop the loop once start() ends, before the child.
    loop.schedule(start());
    ZEXPECT(loop.run() == 0);
    ZEXPECT(storage.destroyed);
    ZEXPECT(storage.untouched());
}

// cancel() ends the children one after another, the last of which frees the
// group under it.
ZEST_CASE(cancel_ending_the_children_owning_the_group_frees_it_once_done) {
    SessionStorage storage;
    auto start = [&]() -> task<> {
        auto session = storage.make();
        session->group.spawn(waits_on(session));
        session->group.spawn(waits_on(session));
        auto& group = session->group;
        session.reset();
        group.cancel();
        co_return;
    };

    auto [result] = run(start());
    ZEXPECT(result.has_value());
    ZEXPECT(storage.destroyed);
    ZEXPECT(storage.untouched());
}

// Outside any task, as from a relay's callback, cancel() is a resumption of
// its own: the children it ends do not free the group before it is done.
ZEST_CASE(cancel_outside_a_task_frees_the_group_once_done) {
    SessionStorage storage;
    task_group<>* group = nullptr;
    auto start = [&]() -> task<> {
        auto session = storage.make();
        session->group.spawn(waits_on(session));
        session->group.spawn(waits_on(session));
        group = &session->group;
        co_return;
    };

    // Returns with the children waiting: nothing else keeps the loop.
    loop.schedule(start());
    ZEXPECT(loop.run() == 0);
    ZASSERT(group != nullptr);
    ZASSERT(!storage.destroyed);
    group->cancel();
    ZEXPECT(storage.destroyed);
    ZEXPECT(storage.untouched());
}

// So is a cancel of the task joining the group, which reaches the group
// through the join.
ZEST_CASE(cancel_of_the_joining_task_frees_the_group_once_done) {
    SessionStorage storage;
    auto join = [](task_group<>& group) -> task<> {
        co_await group.join();
    };
    std::optional<task<>> joining;
    auto start = [&]() -> task<> {
        auto session = storage.make();
        session->group.spawn(waits_on(session));
        session->group.spawn(waits_on(session));
        joining.emplace(join(session->group));
        loop.schedule(*joining);
        co_return;
    };

    // Returns with the children and the join waiting.
    loop.schedule(start());
    ZEXPECT(loop.run() == 0);
    ZASSERT(joining.has_value());
    ZASSERT(!storage.destroyed);
    joining->cancel();
    ZEXPECT(joining->is_cancelled());
    ZEXPECT(storage.destroyed);
    ZEXPECT(storage.untouched());
}

// join() returns once the children are gone, with what their frames held: a
// child the cancel ended where it waited holds the joining task's mutex.
ZEST_CASE(join_returns_once_what_the_children_held_is_gone) {
    event never;
    auto child = [&](mutex& m) -> task<> {
        auto guard = co_await m.scoped_lock();
        co_await never.wait();
    };
    auto driver = [&]() -> task<bool> {
        mutex m;
        task_group<> group;
        group.spawn(child(m));
        group.cancel();
        co_await group.join();
        bool unlocked = m.try_lock();
        if(unlocked) {
            m.unlock();
        }
        co_return unlocked;
    };

    auto [unlocked] = run(driver());
    ZASSERT(unlocked.has_value());
    ZEXPECT(*unlocked);
}

// MSVC's coroutine codegen under ASan cannot destroy the frame of the child
// that settles a group, which waits for the end of the resumption there.
#if !KOTATSU_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
// So does the cancel of the task joining the group: the task awaiting that
// one sees the children gone.
ZEST_CASE(cancelled_join_ends_once_what_the_children_held_is_gone) {
    event never;
    mutex m;
    cancellation_source source;
    bool cancelled = false;
    auto child = [&]() -> task<> {
        auto guard = co_await m.scoped_lock();
        co_await never.wait();
    };
    auto join = [&]() -> task<> {
        task_group<> group;
        group.spawn(child());
        co_await group.join();
    };
    auto driver = [&]() -> task<bool> {
        auto joined = co_await with_token(join(), source.token());
        cancelled = joined.is_cancelled();
        bool unlocked = m.try_lock();
        if(unlocked) {
            m.unlock();
        }
        co_return unlocked;
    };
    auto canceller = [&]() -> task<> {
        source.cancel();
        co_return;
    };

    auto [unlocked, cancelling] = run(driver(), canceller());
    ZASSERT(unlocked.has_value());
    ZEXPECT(*unlocked);
    ZEXPECT(cancelled);
}

// A cancel the leaf completes later settles the groups inside each other at
// once: the outer one frees the inner one's task, frame and all, as the inner
// one settles. The inner group's children go before, while what they hold
// of that task is there.
ZEST_CASE(group_frees_its_children_before_the_task_awaiting_it_ends) {
    struct Alive {
        bool& alive;

        explicit Alive(bool& alive) : alive(alive) {
            alive = true;
        }

        ~Alive() {
            alive = false;
        }
    };

    struct Witness {
        bool& alive;
        bool& outlived;

        ~Witness() {
            if(!alive) {
                outlived = true;
            }
        }
    };

    test::PendingOp op;
    bool inner_alive = false;
    bool outlived = false;
    auto leaf = [&]() -> task<> {
        Witness witness{.alive = inner_alive, .outlived = outlived};
        co_await op;
    };
    auto inner = [&]() -> task<> {
        Alive marker(inner_alive);
        task_group<> group;
        group.spawn(leaf());
        co_await group.join();
    };
    auto driver = [&]() -> task<> {
        task_group<> outer;
        outer.spawn(inner());
        outer.cancel();
        co_await outer.join();
    };
    auto finisher = [&]() -> task<> {
        op.complete();
        co_return;
    };

    auto [result, finished] = run(driver(), finisher());
    ZEXPECT(result.has_value());
    ZEXPECT(op.cancel_requested());
    ZEXPECT(!inner_alive);
    ZEXPECT(!outlived);
}

#endif

// A frame that goes with the group may not start new children in it.
ZEST_CASE(frame_going_with_the_group_cannot_spawn) {
    struct SpawnsAsItGoes {
        task_group<>& group;
        std::optional<bool>& spawned;

        SpawnsAsItGoes(task_group<>& group, std::optional<bool>& spawned) :
            group(group), spawned(spawned) {}

        ~SpawnsAsItGoes() {
            spawned = group.spawn(nothing());
        }

        static task<> nothing() {
            co_return;
        }
    };

    std::optional<bool> spawned;
    auto child = [](std::unique_ptr<SpawnsAsItGoes>) -> task<> {
        co_return;
    };
    auto driver = [&]() -> task<> {
        // The child ends as it is spawned, and goes with the group.
        task_group<> group;
        group.spawn(child(std::make_unique<SpawnsAsItGoes>(group, spawned)));
        co_return;
    };

    auto [result] = run(driver());
    ZEXPECT(result.has_value());
    ZASSERT(spawned.has_value());
    ZEXPECT(!*spawned);
}

};  // ZEST_SUITE(async_runtime_task_group_lifetime)

}  // namespace

}  // namespace kotatsu::async
