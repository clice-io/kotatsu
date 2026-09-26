// ZEST_SUITE(task): direct task<> semantics — co_await chaining, up/down
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

ZEST_SUITE(task){

    ZEST_CASE(task_await){static auto foo = []() -> task<int> {
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
    EXPECT(res == 1);
}

{
    auto [res, res1] = run(foo(), foo1());
    EXPECT(res == 1);
    EXPECT(res1 == 2);
}

{
    auto [res, res1, res2] = run(foo(), foo1(), foo2());
    EXPECT(res == 1);
    EXPECT(res1 == 2);
    EXPECT(res2 == 3);
}

}  // namespace

ZEST_CASE(up_cancel) {
    static auto bar = [](int& x) -> task<int> {
        x += 1;
        co_return 1;
    };

    {
        int x = 0;
        auto task = bar(x);
        task->cancel();
        run(task);
        EXPECT(task->is_cancelled());
        EXPECT(x == 0);
    }
}

ZEST_CASE(down_cancel) {
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
        EXPECT(task->is_cancelled());
        EXPECT(x == 1);
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
        EXPECT(task->is_finished());
        EXPECT(!task.result());
        EXPECT(x == 2);
    }
}

#if KOTA_ENABLE_EXCEPTIONS
ZEST_CASE(exception_propagation) {
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

ZEST_CASE(or_fail_rethrows_child_exception) {
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

ZEST_CASE(dump_dot_basic) {
    bool checked = false;

    auto inner = []() -> task<int> {
        co_return 42;
    };

    auto outer = [&]() -> task<> {
        auto t = inner();
        auto* node = t.operator->();
        auto dot = dump_dot(*node);
        EXPECT(!dot.empty());
        EXPECT(zest::contains(dot, "digraph"));
        EXPECT(zest::contains(dot, "Task"));
        checked = true;
        co_await std::move(t);
    };

    auto t = outer();
    event_loop loop;
    loop.schedule(t);
    loop.run();
    EXPECT(checked);
}

};  // namespace kota

}  // namespace

}  // namespace kota
