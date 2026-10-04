#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "async/harness/exceptions.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/async/async.h"

namespace kota {

namespace {

struct CustomError {
    int code = 0;
};

/// Waits on `gate` in a task of its own, whose cancellation the caller can
/// catch.
task<> wait_on(event& gate) {
    co_await gate.wait();
}

ZEST_SUITE(async_runtime_task_group_errors, zest::LoopFixture) {

ZEST_CASE(join_reports_the_first_error_and_cancels_the_rest) {
    event first_gate;
    event second_gate;
    event slow_gate;
    auto failing = [&](event& gate, error err) -> task<int, error> {
        co_await gate.wait();
        co_await fail(err);
    };
    auto slow = [&]() -> task<> {
        co_await slow_gate.wait();
    };
    auto driver = [&]() -> task<std::vector<error>> {
        task_group<error> group;
        group.spawn(failing(first_gate, error::connection_refused));
        group.spawn(failing(second_gate, error::connection_reset_by_peer));
        group.spawn(slow());
        auto joined = co_await group.join();
        if(joined.has_error()) {
            co_return std::move(joined).error();
        }
        co_return std::vector<error>{};
    };
    auto trigger = [&]() -> task<> {
        first_gate.set();
        co_return;
    };

    auto [result, drove] = run(driver(), trigger());
    ZASSERT(result.has_value());
    ZEXPECT(*result == std::vector{error::connection_refused});
    // Neither gate is ever set: their waits went because the error's cancel
    // reached them.
    ZEXPECT(!second_gate.has_waiters());
    ZEXPECT(!slow_gate.has_waiters());
}

// The children the first error cancels fail too; join() reports every error
// in the order the children failed, not the order they were spawned in.
ZEST_CASE(join_reports_errors_in_the_order_the_children_failed) {
    event gate;
    auto failing_when_cancelled = [&]() -> task<void, error> {
        co_await wait_on(gate).catch_cancel();
        co_await fail(error::connection_reset_by_peer);
    };
    auto failing = []() -> task<void, error> {
        co_await yield();
        co_await fail(error::connection_refused);
    };
    auto driver = [&]() -> task<std::vector<error>> {
        task_group<error> group;
        group.spawn(failing_when_cancelled());
        group.spawn(failing());
        auto joined = co_await group.join();
        if(joined.has_error()) {
            co_return std::move(joined).error();
        }
        co_return std::vector<error>{};
    };

    auto [result] = run(driver());
    ZASSERT(result.has_value());
    ZEXPECT(*result == std::vector{error::connection_refused, error::connection_reset_by_peer});
}

ZEST_CASE(join_reports_errors_of_mixed_types) {
    using Errors = std::variant<error, CustomError>;
    event gate;
    auto failing = []() -> task<int, CustomError> {
        co_await yield();
        co_await fail(CustomError{.code = 7});
    };
    auto slow = [&]() -> task<> {
        co_await gate.wait();
    };
    auto driver = [&]() -> task<std::vector<Errors>> {
        task_group<error, CustomError> group;
        group.spawn(failing());
        group.spawn(slow());
        auto joined = co_await group.join();
        if(joined.has_error()) {
            co_return std::move(joined).error();
        }
        co_return std::vector<Errors>{};
    };

    auto [result] = run(driver());
    ZASSERT(result.has_value());
    ZASSERT(result->size() == 1U);
    ZASSERT(std::holds_alternative<CustomError>(result->front()));
    ZEXPECT(std::get<CustomError>(result->front()).code == 7);
}

ZEST_CASE(error_handled_inside_a_child_does_not_reach_the_group) {
    bool sibling_finished = false;
    auto failing = []() -> task<int, error> {
        co_await yield();
        co_await fail(error::connection_refused);
    };
    auto handling = [&]() -> task<> {
        [[maybe_unused]] auto result = co_await failing();
    };
    auto sibling = [&]() -> task<> {
        co_await yield();
        co_await yield();
        sibling_finished = true;
    };
    auto driver = [&]() -> task<> {
        task_group<> group;
        group.spawn(handling());
        group.spawn(sibling());
        co_await group.join();
    };

    auto [result] = run(driver());
    ZEXPECT(result.has_value());
    ZEXPECT(sibling_finished);
}

// A child that fails while its group is being cancelled from outside keeps
// its error: join() still resumes and reports it, and the joiner ends
// cancelled afterwards.
ZEST_CASE(child_error_survives_an_external_cancel) {
    event gate;
    std::vector<error> reported;
    auto child = [&]() -> task<void, error> {
        auto waited = co_await wait_on(gate).catch_cancel();
        if(waited.is_cancelled()) {
            co_await fail(error::connection_refused);
        }
    };
    auto driver = [&]() -> task<> {
        task_group<error> group;
        group.spawn(child());
        auto joined = co_await group.join();
        if(joined.has_error()) {
            reported = std::move(joined).error();
        }
    };
    auto target = driver();
    auto cancel_it = [&]() -> task<> {
        target.cancel();
        co_return;
    };

    auto [result, drove] = run(target, cancel_it());
    ZEXPECT(reported == std::vector{error::connection_refused});
    ZEXPECT(result.is_cancelled());
}

#if KOTA_ENABLE_EXCEPTIONS

// Reads what was thrown; see test::exceptions_unreadable.
ZEST_CASE(exception_fails_the_joiner_and_cancels_the_rest, skip = test::exceptions_unreadable) {
    event gate;
    auto thrower = []() -> task<> {
        co_await yield();
        throw std::runtime_error("group boom");
    };
    auto slow = [&]() -> task<> {
        co_await gate.wait();
    };
    auto driver = [&]() -> task<> {
        task_group<> group;
        group.spawn(thrower());
        group.spawn(slow());
        co_await group.join();
    };

    ZEXPECT(test::thrown([&] { run(driver()); }) == "group boom");
    ZEXPECT(!gate.has_waiters());
}

// Reads what was thrown; see test::exceptions_unreadable.
ZEST_CASE(exception_thrown_while_spawning_reaches_join, skip = test::exceptions_unreadable) {
    auto thrower = []() -> task<> {
        throw std::runtime_error("at once");
        co_return;
    };
    auto driver = [&]() -> task<> {
        task_group<> group;
        group.spawn(thrower());
        co_await group.join();
    };

    ZEXPECT(test::thrown([&] { run(driver()); }) == "at once");
}

// Reads what was thrown; see test::exceptions_unreadable.
ZEST_CASE(exception_outranks_an_error, skip = test::exceptions_unreadable) {
    event gate;
    auto thrower = [&]() -> task<int, error> {
        co_await wait_on(gate).catch_cancel();
        throw std::runtime_error("boom");
    };
    auto failing = []() -> task<int, error> {
        co_await yield();
        co_await fail(error::connection_refused);
    };
    auto driver = [&]() -> task<> {
        task_group<error> group;
        group.spawn(thrower());
        group.spawn(failing());
        [[maybe_unused]] auto joined = co_await group.join();
    };

    ZEXPECT(test::thrown([&] { run(driver()); }) == "boom");
}

#if !KOTA_WORKAROUND_WINDOWS_ASAN_COROUTINE_EXCEPTION
// The children the first exception cancels throw too; join() rethrows the
// first one thrown, as when_all does, not that of the first child spawned.
// Reads what was thrown; see test::exceptions_unreadable.
ZEST_CASE(join_rethrows_the_first_exception_thrown, skip = test::exceptions_unreadable) {
    event gates[3];
    const char* names[] = {"first spawned", "first thrown", "third spawned"};
    auto thrower = [&](int id) -> task<> {
        co_await wait_on(gates[id]).catch_cancel();
        throw std::runtime_error(names[id]);
    };
    auto driver = [&]() -> task<> {
        task_group<> group;
        group.spawn(thrower(0));
        group.spawn(thrower(1));
        group.spawn(thrower(2));
        co_await group.join();
    };
    auto trigger = [&]() -> task<> {
        gates[1].set();
        co_return;
    };

    ZEXPECT(test::thrown([&] { run(driver(), trigger()); }) == "first thrown");
}
#endif  // !KOTA_WORKAROUND_WINDOWS_ASAN_COROUTINE_EXCEPTION

#endif  // KOTA_ENABLE_EXCEPTIONS

};  // ZEST_SUITE(async_runtime_task_group_errors)

}  // namespace

}  // namespace kota
