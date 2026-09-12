// TEST_SUITE(task): direct task<> semantics — co_await chaining, up/down
// cancellation, exception propagation (co_await and or_fail), and dump_dot on a
// live task node. Cooperative yield() ordering lives in yield_tests.cpp; the
// aggregate combinators in when/.
#include <stdexcept>

#include "../loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/async/async.h"

namespace kota {

namespace {

TEST_SUITE(task) {

TEST_CASE(task_await) {
    static auto foo = []() -> task<int> {
        co_return 1;
    };

    static auto foo1 = []() -> task<int> {
        co_return co_await foo() + 1;
    };

    static auto foo2 = []() -> task<int> {
        auto res = co_await foo();
        auto res1 = co_await foo1();
        co_return res + res1;
    };

// Visual Studio issue:
// https://developercommunity.visualstudio.com/t/Unable-to-destroy-C20-coroutine-in-fin/10657377
#if !KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    {
        event_loop loop;
        loop.schedule(foo());
        loop.run();
    }
#endif

    {
        auto [res] = run(foo());
        EXPECT_EQ(res, 1);
    }

    {
        auto [res, res1] = run(foo(), foo1());
        EXPECT_EQ(res, 1);
        EXPECT_EQ(res1, 2);
    }

    {
        auto [res, res1, res2] = run(foo(), foo1(), foo2());
        EXPECT_EQ(res, 1);
        EXPECT_EQ(res1, 2);
        EXPECT_EQ(res2, 3);
    }
}

TEST_CASE(up_cancel) {
    static auto bar = [](int& x) -> task<int> {
        x += 1;
        co_return 1;
    };

    {
        int x = 0;
        auto task = bar(x);
        task->cancel();
        run(task);
        EXPECT_TRUE(task->is_cancelled());
        EXPECT_EQ(x, 0);
    }
}

TEST_CASE(pre_cancel_await) {
    static auto bar = [](int& x) -> task<int> {
        x += 1;
        co_return 1;
    };

    // Awaiting a task cancelled before it ever started must not run its
    // body; the cancellation propagates to the awaiting task.
    {
        int x = 0;
        bool after_await = false;
        auto outer = [&]() -> task<> {
            auto inner = bar(x);
            inner->cancel();
            co_await std::move(inner);
            after_await = true;
        };

        auto task = outer();
        run(task);
        EXPECT_TRUE(task->is_cancelled());
        EXPECT_EQ(x, 0);
        EXPECT_FALSE(after_await);
    }

    // catch_cancel() observes the pre-cancellation as a value instead.
    {
        int x = 0;
        bool cancelled_seen = false;
        auto outer = [&]() -> task<> {
            auto inner = bar(x);
            inner->cancel();
            auto res = co_await std::move(inner).catch_cancel();
            cancelled_seen = res.is_cancelled();
        };

        auto task = outer();
        run(task);
        EXPECT_TRUE(task->is_finished());
        EXPECT_TRUE(cancelled_seen);
        EXPECT_EQ(x, 0);
    }
}

TEST_CASE(pre_cancel_scheduled_root) {
    static auto bar = [](int& x) -> task<int> {
        x += 1;
        co_return 1;
    };

    // A pre-cancelled task scheduled by rvalue (loop-owned root) must be
    // finalized and its frame reclaimed without running the body.
    int x = 0;
    {
        event_loop loop;
        auto task = bar(x);
        task->cancel();
        loop.schedule(std::move(task));
        loop.run();
    }
    EXPECT_EQ(x, 0);
}

TEST_CASE(down_cancel) {
    static auto bar1 = [](int& x) -> task<> {
        x += 1;
        co_await cancel();
    };

    static auto bar2 = [](int& x) -> task<> {
        co_await bar1(x);
        x += 1;
    };

    {
        int x = 0;
        auto task = bar2(x);
        run(task);
        EXPECT_TRUE(task->is_cancelled());
        EXPECT_EQ(x, 1);
    }

    static auto bar3 = [](int& x) -> task<bool> {
        auto res = co_await bar1(x).catch_cancel();
        x += 1;
        co_return res.has_value();
    };

    {
        int x = 0;
        auto task = bar3(x);
        run(task);
        EXPECT_TRUE(task->is_finished());
        EXPECT_FALSE(task.result());
        EXPECT_EQ(x, 2);
    }
}

#if KOTA_ENABLE_EXCEPTIONS
TEST_CASE(exception_propagation) {
    auto bar1 = []() -> task<> {
        throw std::runtime_error("Test exception");
        co_return;
    };

    auto bar2 = [&]() -> task<> {
        co_return co_await bar1();
    };

    EXPECT_THROWS(run(bar1()));
    EXPECT_THROWS(run(bar2()));
}

TEST_CASE(or_fail_rethrows_child_exception) {
    auto child = []() -> task<int, error> {
        throw std::runtime_error("or_fail child exception");
        co_return 0;
    };

    auto parent = [&]() -> task<int, error> {
        co_return co_await child().or_fail();
    };

    EXPECT_THROWS(run(parent()));
}
#endif

TEST_CASE(dump_dot_basic) {
    bool checked = false;

    auto inner = []() -> task<int> {
        co_return 42;
    };

    auto outer = [&]() -> task<> {
        auto t = inner();
        auto* node = t.operator->();
        auto dot = dump_dot(*node);
        EXPECT_TRUE(!dot.empty());
        EXPECT_TRUE(dot.find("digraph") != std::string::npos);
        EXPECT_TRUE(dot.find("Task") != std::string::npos);
        checked = true;
        co_await std::move(t);
    };

    auto t = outer();
    event_loop loop;
    loop.schedule(t);
    loop.run();
    EXPECT_TRUE(checked);
}

};  // TEST_SUITE(task)

}  // namespace

}  // namespace kota
