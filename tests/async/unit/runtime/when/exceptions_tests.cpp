#include <stdexcept>
#include <tuple>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/async/async.h"

namespace kota {

namespace {

#if KOTA_ENABLE_EXCEPTIONS

ZEST_SUITE(async_runtime_when_exceptions, test::LoopFixture) {

ZEST_CASE(all_exception_cancels_the_rest_and_rethrows) {
    event gate;
    event go;
    bool slow_finished = false;
    auto thrower = [&]() -> task<int> {
        co_await go.wait();
        throw std::runtime_error("boom");
    };
    auto slow = [&]() -> task<int> {
        co_await gate.wait();
        slow_finished = true;
        co_return 2;
    };
    auto combined = [&]() -> task<std::tuple<int, int>> {
        co_return co_await when_all(thrower(), slow());
    };
    auto driver = [&]() -> task<> {
        go.set();
        co_return;
    };

    EXPECT_THROWS(run(combined(), driver()));
    EXPECT(!slow_finished);
    EXPECT(gate.get_head() == nullptr);
}

ZEST_CASE(all_exception_while_armed_starts_no_later_child) {
    int started = 0;
    auto thrower = []() -> task<int> {
        throw std::runtime_error("immediate");
        co_return 0;
    };
    auto later = [&]() -> task<int> {
        started += 1;
        co_return 1;
    };
    auto combined = [&]() -> task<std::tuple<int, int>> {
        co_return co_await when_all(thrower(), later());
    };

    EXPECT_THROWS(run(combined()));
    EXPECT(started == 0);
}

ZEST_CASE(any_exception_cancels_the_rest_and_rethrows) {
    event gate;
    event go;
    bool slow_finished = false;
    auto thrower = [&]() -> task<int> {
        co_await go.wait();
        throw std::runtime_error("boom");
    };
    auto slow = [&]() -> task<int> {
        co_await gate.wait();
        slow_finished = true;
        co_return 2;
    };
    auto combined = [&]() -> task<> {
        co_await when_any(thrower(), slow());
    };
    auto driver = [&]() -> task<> {
        go.set();
        co_return;
    };

    EXPECT_THROWS(run(combined(), driver()));
    EXPECT(!slow_finished);
}

ZEST_CASE(range_exception_rethrows) {
    event gate;
    bool slow_finished = false;
    auto thrower = []() -> task<int> {
        throw std::runtime_error("range boom");
        co_return 0;
    };
    auto slow = [&]() -> task<int> {
        co_await gate.wait();
        slow_finished = true;
        co_return 2;
    };
    auto all = [&]() -> task<> {
        std::vector<task<int>> tasks;
        tasks.push_back(slow());
        tasks.push_back(thrower());
        co_await when_all(std::move(tasks));
    };
    auto any = [&]() -> task<> {
        std::vector<task<int>> tasks;
        tasks.push_back(slow());
        tasks.push_back(thrower());
        co_await when_any(std::move(tasks));
    };

    EXPECT_THROWS(run(all()));
    EXPECT_THROWS(run(any()));
    EXPECT(!slow_finished);
}

ZEST_CASE(nested_exception_reaches_the_outer_combinator) {
    event gate;
    int finished = 0;
    auto thrower = []() -> task<int> {
        throw std::runtime_error("deep");
        co_return 0;
    };
    auto slow = [&]() -> task<int> {
        co_await gate.wait();
        finished += 1;
        co_return 1;
    };
    auto inner = [&]() -> task<int> {
        auto [a, b] = co_await when_all(slow(), thrower());
        co_return a + b;
    };
    auto outer = [&]() -> task<int> {
        auto [a, b] = co_await when_all(slow(), inner());
        co_return a + b;
    };

    EXPECT_THROWS(run(outer()));
    EXPECT(finished == 0);
}

ZEST_CASE(caught_exception_stays_a_value) {
    auto thrower = []() -> task<int> {
        throw std::runtime_error("caught");
        co_return 0;
    };
    auto catcher = [&]() -> task<int> {
        try {
            co_return co_await thrower();
        } catch(const std::runtime_error&) {
            co_return -1;
        }
    };
    auto normal = []() -> task<int> {
        co_return 42;
    };
    auto combined = [&]() -> task<std::tuple<int, int>> {
        co_return co_await when_all(catcher(), normal());
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(*result == std::tuple{-1, 42});
}

// A child that cancels the whole scope and then throws still delivers the
// exception: a racing cancellation never swallows it.
ZEST_CASE(exception_outranks_an_external_cancel) {
    event gate;
    event go;
    async_node* scope = nullptr;
    auto thrower = [&]() -> task<int> {
        co_await go.wait();
        scope->cancel();
        throw std::runtime_error("after cancel");
    };
    auto slow = [&]() -> task<int> {
        co_await gate.wait();
        co_return 1;
    };
    auto combined = [&]() -> task<std::tuple<int, int>> {
        co_return co_await when_all(thrower(), slow());
    };
    auto target = combined();
    scope = target.operator->();
    auto driver = [&]() -> task<> {
        go.set();
        co_return;
    };

    EXPECT_THROWS(run(std::move(target), driver()));
}

};  // ZEST_SUITE(async_runtime_when_exceptions)

#endif  // KOTA_ENABLE_EXCEPTIONS

}  // namespace

}  // namespace kota
