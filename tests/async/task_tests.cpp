#include <stdexcept>

#include "eventide/zest/macro.h"
#include "eventide/zest/zest.h"
#include "eventide/common/config.h"
#include "eventide/async/async.h"

namespace eventide {

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
#if !ETD_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
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

#if ETD_ENABLE_EXCEPTIONS
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
        auto dot = node->dump_dot();
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

}  // namespace eventide
