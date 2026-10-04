#include <memory>
#include <string>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "async/harness/pending_op.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

/// Whether with_task_group<Errors...> takes `Body`.
template <typename Body, typename... Errors>
concept group_body = requires(Body body) { with_task_group<Errors...>(std::move(body)); };

using plain_body = task<> (*)(task_group<>&);
using error_body = task<void, error> (*)(task_group<error>&);
using value_body = task<int> (*)(task_group<>&);

ZEST_SUITE(async_runtime_task_group_scoped, test::LoopFixture) {

ZEST_CASE(with_task_group_gives_what_join_gives) {
    EXPECT(zest::type_eq<decltype(with_task_group(plain_body{})), task<>>());
    EXPECT(zest::type_eq<decltype(with_task_group<error>(error_body{})),
                         task<void, std::vector<error>>>());
}

ZEST_CASE(with_task_group_takes_a_body_its_group_takes) {
    STATIC_EXPECT(group_body<plain_body>);
    STATIC_EXPECT(group_body<error_body, error>);
    STATIC_EXPECT(!group_body<error_body>);
    STATIC_EXPECT(!group_body<value_body>);
}

ZEST_CASE(with_task_group_waits_for_what_the_body_spawned) {
    event gate;
    std::vector<std::string> order;
    auto child = [&]() -> task<> {
        co_await gate.wait();
        order.emplace_back("child");
    };
    auto driver = [&]() -> task<> {
        co_await with_task_group([&](task_group<>& group) -> task<> {
            group.spawn(child());
            order.emplace_back("body");
            co_return;
        });
        order.emplace_back("joined");
    };
    auto opener = [&]() -> task<> {
        co_await yield();
        order.emplace_back("open");
        gate.set();
    };

    auto [result, opened] = run(driver(), opener());
    EXPECT(result.has_value());
    EXPECT(opened.has_value());
    EXPECT(order == std::vector<std::string>{"body", "open", "child", "joined"});
}

// The body's captures live in with_task_group's frame: a child reads them
// after the body has ended, and the lambda the task was made from is gone.
ZEST_CASE(with_task_group_keeps_the_body_captures_for_the_children) {
    event gate;
    auto alive = std::make_shared<bool>(true);

    struct Probe {
        std::shared_ptr<bool> alive;

        Probe(std::shared_ptr<bool> alive) : alive(std::move(alive)) {}

        Probe(Probe&&) = default;

        ~Probe() {
            if(alive) {
                *alive = false;
            }
        }
    };

    bool seen_alive = false;
    auto reader = [&](const Probe&) -> task<> {
        co_await gate.wait();
        seen_alive = *alive;
    };
    auto driver = [&]() -> task<> {
        auto scoped = with_task_group([&, probe = Probe(alive)](task_group<>& group) -> task<> {
            group.spawn(reader(probe));
            co_return;
        });
        co_await std::move(scoped);
    };
    auto opener = [&]() -> task<> {
        co_await yield();
        gate.set();
    };

    auto [result, opened] = run(driver(), opener());
    EXPECT(result.has_value());
    EXPECT(opened.has_value());
    EXPECT(seen_alive);
    EXPECT(!*alive);
}

ZEST_CASE(with_task_group_child_failure_cancels_the_body_fails) {
    event gate;
    bool body_finished = false;
    auto failing = []() -> task<void, error> {
        co_await yield();
        co_await fail(error::connection_refused);
    };
    auto driver = [&]() -> task<void, std::vector<error>> {
        return with_task_group<error>([&](task_group<error>& group) -> task<> {
            group.spawn(failing());
            co_await gate.wait();
            body_finished = true;
        });
    };

    auto [result] = run(driver());
    ASSERT(result.has_error());
    EXPECT(result.error() == std::vector{error::connection_refused});
    EXPECT(!body_finished);
    EXPECT(!gate.has_waiters());
}

ZEST_CASE(with_task_group_body_failure_cancels_the_children_fails) {
    event gate;
    auto waiting = [&]() -> task<> {
        co_await gate.wait();
    };
    auto driver = [&]() -> task<void, std::vector<error>> {
        return with_task_group<error>([&](task_group<error>& group) -> task<void, error> {
            group.spawn(waiting());
            co_await yield();
            co_await fail(error::connection_refused);
        });
    };

    auto [result] = run(driver());
    ASSERT(result.has_error());
    EXPECT(result.error() == std::vector{error::connection_refused});
    EXPECT(!gate.has_waiters());
}

// A body that ends cancelled is a child like any other: the children it
// spawned run on, and with_task_group succeeds once they have ended.
ZEST_CASE(with_task_group_body_cancel_leaves_the_children_running) {
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
    auto opener = [&]() -> task<> {
        co_await yield();
        gate.set();
    };

    auto [result, opened] = run(driver(), opener());
    EXPECT(result.has_value());
    EXPECT(opened.has_value());
    EXPECT(child_finished);
}

// A cancel of the awaiting task cancels every child, and the task ends
// cancelled only once the last child has.
ZEST_CASE(with_task_group_cancel_waits_for_every_child) {
    test::PendingOp op;
    auto pending = [&]() -> task<> {
        co_await op;
    };
    auto scoped = with_task_group([&](task_group<>& group) -> task<> {
        group.spawn(pending());
        co_return;
    });
    auto canceller = [&]() -> task<bool> {
        co_await yield();
        scoped.cancel();
        bool ended_before_the_child = scoped.done();
        op.complete();
        co_return ended_before_the_child;
    };

    auto [result, ended_early] = run(scoped, canceller());
    EXPECT(result.is_cancelled());
    EXPECT(op.cancel_requested());
    ASSERT(ended_early.has_value());
    EXPECT(!*ended_early);
}

#if KOTA_ENABLE_EXCEPTIONS

// Reads what was thrown; see test::exceptions_unreadable.
ZEST_CASE(with_task_group_rethrows_what_a_child_threw, skip = test::exceptions_unreadable) {
    auto thrower = []() -> task<> {
        co_await yield();
        throw std::runtime_error("child boom");
    };
    auto driver = [&]() -> task<> {
        co_await with_task_group([&](task_group<>& group) -> task<> {
            group.spawn(thrower());
            co_return;
        });
    };

    EXPECT(test::thrown([&] { run(driver()); }) == "child boom");
}

#endif

};  // ZEST_SUITE(async_runtime_task_group_scoped)

}  // namespace

}  // namespace kota
