#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "async/harness/pending_op.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/async/async.h"

namespace kota {

namespace {

struct CustomError {
    int code = 0;
};

/// Whether with_task_group<Errors...> takes a body that returns `Task`.
template <typename Task, typename... Errors>
concept takes_body =
    requires(Task (&body)(task_group<Errors...>&)) { with_task_group<Errors...>(body); };

ZEST_SUITE(async_runtime_task_group_scoped, test::LoopFixture) {

ZEST_CASE(body_returns_a_task_without_a_value_the_group_takes) {
    STATIC_EXPECT(takes_body<task<>>);
    STATIC_EXPECT(!takes_body<task<int>>);
    STATIC_EXPECT(!takes_body<task<void, error>>);
    STATIC_EXPECT(takes_body<task<>, error>);
    STATIC_EXPECT(takes_body<task<void, error>, error>);
    STATIC_EXPECT(!takes_body<task<void, CustomError>, error>);
    STATIC_EXPECT(takes_body<task<void, CustomError>, error, CustomError>);
}

// The helper returns only once every child has ended: the body, which
// suspends, and what it spawned, which runs on after the body has ended.
ZEST_CASE(helper_returns_once_every_child_has_ended) {
    event gate;
    event spawned;
    int finished = 0;
    auto child = [&]() -> task<> {
        co_await gate.wait();
        finished += 1;
    };
    auto driver = [&]() -> task<int> {
        co_await with_task_group([&](task_group<>& group) -> task<> {
            group.spawn(child());
            co_await yield();
            group.spawn(child());
            // The releaser runs once this resumption is over: after the body
            // has ended.
            spawned.set();
        });
        co_return finished;
    };
    auto releaser = [&]() -> task<> {
        co_await spawned.wait();
        gate.set();
    };

    auto [result, released] = run(driver(), releaser());
    ASSERT(result.has_value());
    EXPECT(*result == 2);
}

// A cancel of the task awaiting the helper cancels every child, the body
// included, and waits for each to end, however long its cancellation takes;
// the task then ends cancelled.
ZEST_CASE(cancel_of_the_awaiting_task_cancels_and_awaits_every_child) {
    event gate;
    test::PendingOp op;
    bool returned = false;
    auto waiting = [&]() -> task<> {
        co_await gate.wait();
    };
    auto pending = [&]() -> task<> {
        co_await op;
    };
    auto driver = [&]() -> task<> {
        co_await with_task_group([&](task_group<>& group) -> task<> {
            group.spawn(waiting());
            group.spawn(pending());
            co_await gate.wait();
        });
        returned = true;
    };
    auto target = driver();
    auto canceler = [&]() -> task<bool> {
        target.cancel();
        bool done_before = target.done();
        op.complete();
        co_return done_before;
    };

    auto [result, done_before] = run(target, canceler());
    EXPECT(result.is_cancelled());
    ASSERT(done_before.has_value());
    EXPECT(!*done_before);
    EXPECT(op.cancel_requested());
    // The gate is never set: both waits went because the cancel reached them.
    EXPECT(!gate.has_waiters());
    EXPECT(!returned);
}

// A child that fails cancels the others, the body included, and awaiting the
// helper gives its error as join() does.
ZEST_CASE(child_error_cancels_the_rest_and_is_reported) {
    event gate;
    auto failing = []() -> task<void, error> {
        co_await yield();
        co_await fail(error::connection_refused);
    };
    auto waiting = [&]() -> task<> {
        co_await gate.wait();
    };
    auto driver = [&]() -> task<std::vector<error>> {
        auto joined = co_await with_task_group<error>([&](task_group<error>& group) -> task<> {
            group.spawn(waiting());
            group.spawn(failing());
            co_await gate.wait();
        });
        EXPECT(zest::type_eq<decltype(joined), task_group<error>::result_type>());
        if(joined.has_error()) {
            co_return std::move(joined).error();
        }
        co_return std::vector<error>{};
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(*result == std::vector{error::connection_refused});
    // The gate is never set: both waits went because the error's cancel
    // reached them.
    EXPECT(!gate.has_waiters());
}

// The body is a child like the others: when it fails, it cancels what it
// spawned.
ZEST_CASE(body_error_cancels_its_children) {
    event gate;
    auto waiting = [&]() -> task<> {
        co_await gate.wait();
    };
    auto driver = [&]() -> task<std::vector<error>> {
        auto joined =
            co_await with_task_group<error>([&](task_group<error>& group) -> task<void, error> {
                group.spawn(waiting());
                co_await yield();
                co_await fail(error::connection_refused);
            });
        if(joined.has_error()) {
            co_return std::move(joined).error();
        }
        co_return std::vector<error>{};
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(*result == std::vector{error::connection_refused});
    EXPECT(!gate.has_waiters());
}

// The body ending cancelled just ends, as any child's cancellation does: what
// it spawned runs on, and the helper returns normally once that has ended.
ZEST_CASE(body_cancel_leaves_its_children_running) {
    event gate;
    bool child_finished = false;
    auto child = [&]() -> task<> {
        co_await gate.wait();
        child_finished = true;
    };
    auto driver = [&]() -> task<> {
        co_await with_task_group([&](task_group<>& group) -> task<> {
            group.spawn(child());
            co_await cancel();
        });
    };
    auto releaser = [&]() -> task<> {
        gate.set();
        co_return;
    };

    auto [result, released] = run(driver(), releaser());
    EXPECT(result.has_value());
    EXPECT(child_finished);
}

// The body is kept in the helper's frame: what it captures stays alive while
// it and its children run, however long they suspend, and goes with the
// helper. The body alone owns `owned`, so the use count tells whether it is
// alive.
ZEST_CASE(body_captures_live_until_every_child_has_ended) {
    auto owned = std::make_shared<int>(7);
    std::weak_ptr<int> watched = owned;
    // What the body and its child see after suspending.
    std::vector<long> owners;
    std::vector<int> values;
    auto reader = [&](const std::shared_ptr<int>& captured) -> task<> {
        co_await yield();
        co_await yield();
        owners.push_back(watched.use_count());
        values.push_back(*captured);
    };
    auto driver = [&]() -> task<bool> {
        co_await with_task_group([&, kept = std::move(owned)](task_group<>& group) -> task<> {
            // Reads the capture after the body has ended.
            group.spawn(reader(kept));
            co_await yield();
            owners.push_back(watched.use_count());
            values.push_back(*kept);
        });
        co_return watched.expired();
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    EXPECT(*result);
    EXPECT(owners == std::vector<long>{1, 1});
    EXPECT(values == std::vector{7, 7});
}

#if KOTA_ENABLE_EXCEPTIONS

// Reads what was thrown; see test::exceptions_unreadable.
ZEST_CASE(child_exception_is_rethrown, skip = test::exceptions_unreadable) {
    auto thrower = []() -> task<> {
        co_await yield();
        throw std::runtime_error("scoped boom");
    };
    auto driver = [&]() -> task<> {
        co_await with_task_group([&](task_group<>& group) -> task<> {
            group.spawn(thrower());
            co_return;
        });
    };

    EXPECT(test::thrown([&] { run(driver()); }) == "scoped boom");
}

#endif  // KOTA_ENABLE_EXCEPTIONS

};  // ZEST_SUITE(async_runtime_task_group_scoped)

}  // namespace

}  // namespace kota
