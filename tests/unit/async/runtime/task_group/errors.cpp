// TEST_SUITE(task_group_errors): error and exception propagation for task_group
// — structured errors (fail-fast collects first, cancels siblings), mixed error
// types, errors preserved across cancel, and (under KOTA_ENABLE_EXCEPTIONS) C++
// exception propagation/precedence. Plain spawn/join lives in basics.cpp;
// cancellation without errors in cancel.cpp.
#include <stdexcept>
#include <string>

#include "../../loop_fixture.h"
#include "../../support.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

TEST_SUITE(task_group_errors, loop_fixture) {

TEST_CASE(returns_structured_error) {
    int slow_done = 0;

    auto failing = [&]() -> task<int, error> {
        co_await sleep(1, loop);
        co_await fail(error::connection_refused);
    };

    auto slow = [&]() -> task<> {
        co_await sleep(50, loop);
        slow_done += 1;
    };

    auto driver = [&]() -> task<> {
        task_group<error> group(loop);
        group.spawn(failing());
        group.spawn(slow());
        auto res = co_await group.join();
        EXPECT_TRUE(res.has_error());
        EXPECT_EQ(res.error().size(), 1u);
        EXPECT_EQ(res.error()[0], error::connection_refused);
    };

    auto t = driver();
    schedule_all(t);
    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(mixed_error_types) {
    int slow_done = 0;

    auto failing = [&]() -> task<int, custom_error> {
        co_await sleep(1, loop);
        co_await fail(custom_error{7});
    };

    auto slow = [&]() -> task<> {
        co_await sleep(50, loop);
        slow_done += 1;
    };

    auto driver = [&]() -> task<> {
        task_group<error, custom_error> group(loop);
        group.spawn(failing());
        group.spawn(slow());
        auto res = co_await group.join();
        EXPECT_TRUE(res.has_error());
        EXPECT_EQ(res.error().size(), 1u);
        EXPECT_EQ(std::get<custom_error>(res.error()[0]), custom_error{7});
    };

    auto t = driver();
    schedule_all(t);
    EXPECT_TRUE(t->is_finished());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(direct_error_does_not_escape) {
    int slow_done = 0;

    auto failing = [&]() -> task<> {
        auto inner = [&]() -> task<int, error> {
            co_await sleep(1, loop);
            co_await fail(error::connection_refused);
        };
        auto res = co_await inner();
        (void)res;
    };

    auto slow = [&]() -> task<> {
        co_await sleep(5, loop);
        slow_done += 1;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(failing());
        group.spawn(slow());
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
    EXPECT_EQ(slow_done, 1);
}

// Fail-fast cancels siblings after the first error, so the error vector
// always contains exactly 1 entry.  Verify the value is correct.
TEST_CASE(fail_fast_collects_first_error) {
    auto failing = [&](int ms, error e) -> task<int, error> {
        co_await sleep(ms, loop);
        co_await fail(e);
    };

    auto driver = [&]() -> task<> {
        task_group<error> group(loop);
        group.spawn(failing(1, error::connection_refused));
        group.spawn(failing(2, error::connection_reset_by_peer));
        group.spawn(failing(3, error::io_error));
        auto res = co_await group.join();
        EXPECT_TRUE(res.has_error());
        EXPECT_EQ(res.error().size(), 1u);
        EXPECT_EQ(res.error().front(), error::connection_refused);
    };

    auto t = driver();
    schedule_all(t);
    EXPECT_TRUE(t->is_finished());
}

TEST_CASE(fail_fast_cancels_siblings) {
    int completed = 0;

    auto failing = [&]() -> task<int, error> {
        co_await sleep(1, loop);
        co_await fail(error::connection_refused);
    };

    auto slow = [&](int ms) -> task<> {
        co_await sleep(ms, loop);
        completed += 1;
    };

    auto driver = [&]() -> task<> {
        task_group<error> group(loop);
        group.spawn(failing());
        group.spawn(slow(100));
        group.spawn(slow(100));
        group.spawn(slow(100));
        auto res = co_await group.join();
        EXPECT_TRUE(res.has_error());
    };

    auto t = driver();
    schedule_all(t);
    EXPECT_EQ(completed, 0);
}

// cancel() while join() suspended, with errors — errors collected before cancel
TEST_CASE(cancel_while_join_suspended_with_error) {
    task_group<error>* group_ptr = nullptr;

    auto failing = [&]() -> task<int, error> {
        co_await sleep(1, loop);
        co_await fail(error::connection_refused);
    };

    auto slow = [&]() -> task<> {
        co_await sleep(std::chrono::seconds(10), loop);
    };

    auto driver = [&]() -> task<> {
        task_group<error> group(loop);
        group_ptr = &group;
        group.spawn(failing());
        group.spawn(slow());
        auto res = co_await group.join();
        EXPECT_TRUE(res.has_error());
        EXPECT_EQ(res.error().size(), 1u);
        EXPECT_EQ(res.error().front(), error::connection_refused);
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(5, loop);
        group_ptr->abort();
    };

    auto t = driver();
    auto c = canceler();
    schedule_all(t, c);
    EXPECT_TRUE(t->is_finished());
}

// A child that fails while the group is being cancelled externally must not
// have its error swallowed by the cancellation: join() still resumes and
// reports it (errors outrank cancellation), and the joiner finalizes as
// cancelled afterwards.
TEST_CASE(external_cancel_preserves_child_error) {
    bool observed = false;
    async_node* driver_node = nullptr;

    auto child = [&]() -> task<void, error> {
        auto inner = co_await sleep(std::chrono::seconds(10), loop).catch_cancel();
        if(inner.is_cancelled()) {
            co_await fail(error::connection_refused);
        }
    };

    auto driver = [&]() -> task<> {
        task_group<error> group(loop);
        group.spawn(child());
        auto res = co_await group.join();
        EXPECT_TRUE(res.has_error());
        EXPECT_EQ(res.error().size(), 1u);
        EXPECT_EQ(res.error().front(), error::connection_refused);
        observed = true;
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(5, loop);
        driver_node->cancel();
    };

    auto t = driver();
    auto c = canceler();
    driver_node = t.operator->();
    schedule_all(t, c);

    EXPECT_TRUE(t->is_cancelled());
    EXPECT_TRUE(observed);
}

#if KOTA_ENABLE_EXCEPTIONS
TEST_CASE(exception_propagates) {
    int slow_done = 0;

    auto thrower = [&]() -> task<> {
        co_await sleep(1, loop);
        throw std::runtime_error("group boom");
    };

    auto slow = [&]() -> task<> {
        co_await sleep(50, loop);
        slow_done += 1;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(thrower());
        group.spawn(slow());
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
    EXPECT_TRUE(t->is_failed());
    EXPECT_THROWS(t.result());
    EXPECT_EQ(slow_done, 0);
}

TEST_CASE(exception_takes_precedence_over_error) {
    auto thrower = [&]() -> task<int, error> {
        co_await sleep(1, loop);
        throw std::runtime_error("boom");
    };

    auto failing = [&]() -> task<int, error> {
        co_await sleep(2, loop);
        co_await fail(error::connection_refused);
    };

    auto driver = [&]() -> task<> {
        task_group<error> group(loop);
        group.spawn(thrower());
        group.spawn(failing());
        auto res = co_await group.join();
        (void)res;
    };

    auto t = driver();
    schedule_all(t);
    EXPECT_TRUE(t->is_failed());
    EXPECT_THROWS(t.result());
}

#if !KOTA_WORKAROUND_WINDOWS_ASAN_COROUTINE_EXCEPTION
TEST_CASE(only_first_exception_rethrown) {
    std::string caught_what;

    auto thrower = [&](int ms, const char* msg) -> task<> {
        co_await sleep(ms, loop);
        throw std::runtime_error(msg);
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(thrower(1, "first"));
        group.spawn(thrower(1, "second"));
        group.spawn(thrower(1, "third"));
        try {
            co_await group.join();
        } catch(const std::runtime_error& e) {
            caught_what = e.what();
            throw;
        }
    };

    auto t = driver();
    schedule_all(t);
    EXPECT_TRUE(t->is_failed());
    EXPECT_EQ(caught_what, "first");
}
#endif  // !KOTA_WORKAROUND_WINDOWS_ASAN_COROUTINE_EXCEPTION

// Synchronous exception during spawn (resume_and_drain path)
TEST_CASE(sync_exception_on_spawn) {
    auto thrower = []() -> task<> {
        throw std::runtime_error("immediate");
        co_return;
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group.spawn(thrower());
        co_await group.join();
    };

    auto t = driver();
    schedule_all(t);
    EXPECT_TRUE(t->is_failed());
    EXPECT_THROWS(t.result());
}

// cancel() while join() suspended, exception was already captured
TEST_CASE(cancel_while_join_suspended_with_exception) {
    task_group<>* group_ptr = nullptr;

    auto thrower = [&]() -> task<> {
        co_await sleep(1, loop);
        throw std::runtime_error("boom");
    };

    auto slow = [&]() -> task<> {
        co_await sleep(std::chrono::seconds(10), loop);
    };

    auto driver = [&]() -> task<> {
        task_group<> group(loop);
        group_ptr = &group;
        group.spawn(thrower());
        group.spawn(slow());
        co_await group.join();
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(5, loop);
        group_ptr->abort();
    };

    auto t = driver();
    auto c = canceler();
    schedule_all(t, c);
    EXPECT_TRUE(t->is_failed());
    EXPECT_THROWS(t.result());
}
#endif

};  // TEST_SUITE(task_group_errors)

}  // namespace kota
