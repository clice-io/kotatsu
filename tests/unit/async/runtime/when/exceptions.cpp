// ZEST_SUITE(when_exceptions): C++ exception propagation through when_all/when_any
// (only compiled when KOTA_ENABLE_EXCEPTIONS). Covers a throwing child cancelling
// siblings, immediate throws, range overloads, nested/caught exceptions, empty
// range, and exception beating an external cancel. Structured error (co_await
// fail) propagation lives in errors.cpp.
#include <stdexcept>

#include "../../loop_fixture.h"
#include "../../support.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

#if KOTA_ENABLE_EXCEPTIONS

ZEST_SUITE(when_exceptions){

    ZEST_CASE(all_exception_cancels_siblings){int slow_done = 0;

auto thrower = [&]() -> task<int> {
    co_await sleep(1);
    throw std::runtime_error("boom");
    co_return 0;
};

auto slow = [&]() -> task<int> {
    co_await sleep(50);
    slow_done += 1;
    co_return 2;
};

auto combined = [&]() -> task<int> {
    auto [a, b] = co_await when_all(thrower(), slow());
    co_return a + b;
};

auto t = combined();
EXPECT_THROWS(run(t));

EXPECT(t->is_failed());
EXPECT_THROWS(t.result());
EXPECT(slow_done == 0);

}

// A child that throws after the scope was cancelled must still deliver the
// exception (previously the exception was silently swallowed by the
// Cancelled finalization).
ZEST_CASE(all_exception_beats_external_cancel) {
    async_node* combined_node = nullptr;

    auto thrower = [&]() -> task<int> {
        co_await sleep(1);
        combined_node->cancel();
        throw std::runtime_error("boom");
        co_return 0;
    };

    auto slow = []() -> task<int> {
        co_await sleep(50);
        co_return 1;
    };

    auto combined = [&]() -> task<int> {
        auto [a, b] = co_await when_all(thrower(), slow());
        co_return a + b;
    };

    auto t = combined();
    combined_node = t.operator->();
    EXPECT_THROWS(run(t));
}

ZEST_CASE(all_exception_immediate) {
    auto thrower = []() -> task<int> {
        throw std::runtime_error("immediate boom");
        co_return 0;
    };

    auto normal = []() -> task<int> {
        co_return 42;
    };

    auto combined = [&]() -> task<int> {
        auto [a, b] = co_await when_all(thrower(), normal());
        co_return a + b;
    };

    EXPECT_THROWS(run(combined()));
}

ZEST_CASE(any_exception_cancels_siblings) {
    int slow_done = 0;

    auto thrower = [&]() -> task<int> {
        co_await sleep(1);
        throw std::runtime_error("boom");
        co_return 0;
    };

    auto slow = [&]() -> task<int> {
        co_await sleep(50);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<> {
        co_await when_any(thrower(), slow());
    };

    auto t = combined();
    EXPECT_THROWS(run(t));

    EXPECT(t->is_failed());
    EXPECT_THROWS(t.result());
    EXPECT(slow_done == 0);
}

ZEST_CASE(all_range_exception) {
    int slow_done = 0;

    auto thrower = [&]() -> task<int> {
        co_await sleep(1);
        throw std::runtime_error("range boom");
        co_return 0;
    };

    auto slow = [&]() -> task<int> {
        co_await sleep(50);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<int> {
        small_vector<task<int>> tasks;
        tasks.emplace_back(thrower());
        tasks.emplace_back(slow());
        auto results = co_await when_all(std::move(tasks));
        co_return results[0] + results[1];
    };

    auto t = combined();
    EXPECT_THROWS(run(t));

    EXPECT(t->is_failed());
    EXPECT_THROWS(t.result());
    EXPECT(slow_done == 0);
}

ZEST_CASE(any_range_exception) {
    int slow_done = 0;

    auto thrower = [&]() -> task<int> {
        co_await sleep(1);
        throw std::runtime_error("range any boom");
        co_return 0;
    };

    auto slow = [&]() -> task<int> {
        co_await sleep(50);
        slow_done += 1;
        co_return 2;
    };

    auto combined = [&]() -> task<> {
        small_vector<task<int>> tasks;
        tasks.emplace_back(thrower());
        tasks.emplace_back(slow());
        co_await when_any(std::move(tasks));
    };

    auto t = combined();
    EXPECT_THROWS(run(t));

    EXPECT(t->is_failed());
    EXPECT_THROWS(t.result());
    EXPECT(slow_done == 0);
}

ZEST_CASE(any_range_empty_throws) {
    small_vector<task<int>> tasks;
    EXPECT_THROWS((void)when_any(std::move(tasks)));
}

ZEST_CASE(nested_exception_propagates) {
    auto thrower = [&]() -> task<int> {
        co_await sleep(1);
        throw std::runtime_error("deep boom");
        co_return 0;
    };

    auto inner = [&]() -> task<int> {
        auto [a, b] = co_await when_all(thrower(), delayed_int(50, 1));
        co_return a + b;
    };

    auto outer = [&]() -> task<int> {
        auto [a, b] = co_await when_all(inner(), delayed_int(50, 2));
        co_return a + b;
    };

    auto t = outer();
    EXPECT_THROWS(run(t));

    EXPECT(t->is_failed());
    EXPECT_THROWS(t.result());
}

ZEST_CASE(caught_exception_does_not_propagate) {
    auto thrower = [&]() -> task<int> {
        throw std::runtime_error("caught boom");
        co_return 0;
    };

    auto catcher = [&]() -> task<int> {
        try {
            co_return co_await thrower();
        } catch(const std::runtime_error&) {
            co_return -1;
        }
    };

    auto combined = [&]() -> task<int> {
        auto [a, b] = co_await when_all(catcher(), delayed_int(1, 42));
        co_return a + b;
    };

    auto [res] = run(combined());
    EXPECT(res == 41);
}

ZEST_CASE(direct_co_await_rethrows) {
    auto thrower = []() -> task<int> {
        throw std::runtime_error("direct boom");
        co_return 0;
    };

    auto parent = [&]() -> task<int> {
        co_return co_await thrower();
    };

    EXPECT_THROWS(run(parent()));
}
}
;  // ZEST_SUITE(when_exceptions)

#endif  // KOTA_ENABLE_EXCEPTIONS

}  // namespace kota
