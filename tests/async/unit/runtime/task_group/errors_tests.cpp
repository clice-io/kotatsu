#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/async/async.h"

namespace kota {

namespace {

struct CustomError {
    int code = 0;
};

ZEST_SUITE(async_runtime_task_group_errors, test::LoopFixture) {

ZEST_CASE(join_reports_the_first_error_and_cancels_the_rest) {
    event first_gate;
    event second_gate;
    event slow_gate;
    bool slow_finished = false;
    auto failing = [&](event& gate, error err) -> task<int, error> {
        co_await gate.wait();
        co_await fail(err);
    };
    auto slow = [&]() -> task<> {
        co_await slow_gate.wait();
        slow_finished = true;
    };
    auto driver = [&]() -> task<std::vector<error>> {
        task_group<error> group(loop);
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
    ASSERT(result.has_value());
    EXPECT(*result == std::vector{error::connection_refused});
    EXPECT(!slow_finished);
    EXPECT(second_gate.get_head() == nullptr);
    EXPECT(slow_gate.get_head() == nullptr);
}

ZEST_CASE(join_reports_errors_of_mixed_types) {
    using Errors = std::variant<error, CustomError>;
    event gate;
    auto failing = []() -> task<int, CustomError> {
        co_await yield();
        co_await fail(CustomError{7});
    };
    auto slow = [&]() -> task<> {
        co_await gate.wait();
    };
    auto driver = [&]() -> task<std::vector<Errors>> {
        task_group<error, CustomError> group(loop);
        group.spawn(failing());
        group.spawn(slow());
        auto joined = co_await group.join();
        if(joined.has_error()) {
            co_return std::move(joined).error();
        }
        co_return std::vector<Errors>{};
    };

    auto [result] = run(driver());
    ASSERT(result.has_value());
    ASSERT(result->size() == 1U);
    ASSERT(std::holds_alternative<CustomError>(result->front()));
    EXPECT(std::get<CustomError>(result->front()).code == 7);
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
        task_group<> group(loop);
        group.spawn(handling());
        group.spawn(sibling());
        co_await group.join();
    };

    auto [result] = run(driver());
    EXPECT(result.has_value());
    EXPECT(sibling_finished);
}

// A child that fails while its group is being cancelled from outside keeps
// its error: join() still resumes and reports it, and the joiner ends
// cancelled afterwards.
ZEST_CASE(child_error_survives_an_external_cancel) {
    event gate;
    std::vector<error> reported;
    auto child = [&]() -> task<void, error> {
        auto waited = co_await gate.wait().catch_cancel();
        if(waited.is_cancelled()) {
            co_await fail(error::connection_refused);
        }
    };
    auto driver = [&]() -> task<> {
        task_group<error> group(loop);
        group.spawn(child());
        auto joined = co_await group.join();
        if(joined.has_error()) {
            reported = std::move(joined).error();
        }
    };
    auto target = driver();
    auto* node = target.operator->();
    auto cancel_it = [&]() -> task<> {
        node->cancel();
        co_return;
    };

    auto [result, drove] = run(std::move(target), cancel_it());
    EXPECT(reported == std::vector{error::connection_refused});
    EXPECT(result.is_cancelled());
}

#if KOTA_ENABLE_EXCEPTIONS

ZEST_CASE(exception_fails_the_joiner_and_cancels_the_rest) {
    event gate;
    bool slow_finished = false;
    auto thrower = []() -> task<> {
        co_await yield();
        throw std::runtime_error("group boom");
    };
    auto slow = [&]() -> task<> {
        co_await gate.wait();
        slow_finished = true;
    };
    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(thrower());
        group.spawn(slow());
        co_await group.join();
    };

    EXPECT_THROWS(run(driver()));
    EXPECT(!slow_finished);
}

ZEST_CASE(exception_thrown_while_spawning_reaches_join) {
    auto thrower = []() -> task<> {
        throw std::runtime_error("at once");
        co_return;
    };
    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(thrower());
        co_await group.join();
    };

    EXPECT_THROWS(run(driver()));
}

ZEST_CASE(exception_outranks_an_error) {
    event gate;
    auto thrower = [&]() -> task<int, error> {
        co_await gate.wait().catch_cancel();
        throw std::runtime_error("boom");
    };
    auto failing = []() -> task<int, error> {
        co_await yield();
        co_await fail(error::connection_refused);
    };
    auto driver = [&]() -> task<> {
        task_group<error> group(loop);
        group.spawn(thrower());
        group.spawn(failing());
        [[maybe_unused]] auto joined = co_await group.join();
    };

    EXPECT_THROWS(run(driver()));
}

#if !KOTA_WORKAROUND_WINDOWS_ASAN_COROUTINE_EXCEPTION
// Children cancelled by the first exception throw too; join() rethrows the
// first one.
ZEST_CASE(join_rethrows_the_first_exception) {
    event gates[3];
    const char* names[] = {"first", "second", "third"};
    auto thrower = [&](int id) -> task<> {
        co_await gates[id].wait().catch_cancel();
        throw std::runtime_error(names[id]);
    };
    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(thrower(0));
        group.spawn(thrower(1));
        group.spawn(thrower(2));
        co_await group.join();
    };
    auto trigger = [&]() -> task<> {
        gates[0].set();
        co_return;
    };

    std::string what;
    try {
        run(driver(), trigger());
    } catch(const std::runtime_error& e) {
        what = e.what();
    }
    EXPECT(what == "first");
}
#endif  // !KOTA_WORKAROUND_WINDOWS_ASAN_COROUTINE_EXCEPTION

#endif  // KOTA_ENABLE_EXCEPTIONS

};  // ZEST_SUITE(async_runtime_task_group_errors)

}  // namespace

}  // namespace kota
