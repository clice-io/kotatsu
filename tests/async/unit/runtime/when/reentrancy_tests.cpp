#include <optional>
#include <tuple>
#include <variant>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

// Sync primitives resume the tasks they wake through the loop's deferred
// queue: a child that wakes a sibling runs on to its end first, so it wins a
// when_any and the woken sibling is cancelled instead of resumed.

ZEST_SUITE(async_runtime_when_reentrancy, test::LoopFixture) {

ZEST_CASE(any_setter_of_an_event_wins_over_its_waiters) {
    event ev;
    auto waiter = [&](int id) -> task<int> {
        co_await ev.wait();
        co_return id;
    };
    auto setter = [&]() -> task<int> {
        co_await yield();
        ev.set();
        co_return 3;
    };
    auto combined = [&]() -> task<std::variant<int, int, int>> {
        co_return co_await when_any(waiter(1), waiter(2), setter());
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    ASSERT(result->index() == 2U);
    EXPECT(std::get<2>(*result) == 3);
}

ZEST_CASE(any_releaser_of_a_semaphore_wins_over_its_waiter) {
    semaphore sem;
    auto waiter = [&]() -> task<int> {
        co_await sem.acquire();
        co_return 1;
    };
    auto releaser = [&]() -> task<int> {
        co_await yield();
        sem.release();
        co_return 2;
    };
    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(waiter(), releaser());
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(result->index() == 1U);
    // The cancelled waiter gave its unit back.
    EXPECT(sem.try_acquire());
}

ZEST_CASE(any_unlocker_of_a_mutex_wins_over_its_waiter) {
    mutex m;
    auto holder = [&]() -> task<int> {
        co_await m.lock();
        co_await yield();
        m.unlock();
        co_return 1;
    };
    auto waiter = [&]() -> task<int> {
        co_await m.lock();
        m.unlock();
        co_return 2;
    };
    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(holder(), waiter());
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(result->index() == 0U);
    // The cancelled waiter gave the mutex back.
    EXPECT(m.try_lock());
}

ZEST_CASE(any_notifier_of_a_condition_variable_wins_over_its_waiter) {
    mutex m;
    condition_variable cv;
    auto waiter = [&]() -> task<int> {
        co_await m.lock();
        co_await cv.wait(m);
        m.unlock();
        co_return 1;
    };
    auto notifier = [&]() -> task<int> {
        co_await yield();
        cv.notify_one();
        co_return 2;
    };
    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(waiter(), notifier());
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(result->index() == 1U);
    EXPECT(m.try_lock());
}

ZEST_CASE(all_child_waking_a_sibling_lets_both_finish) {
    event ev;
    int finished = 0;
    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        finished += 1;
    };
    auto setter = [&]() -> task<> {
        co_await yield();
        ev.set();
        finished += 1;
    };
    auto combined = [&]() -> task<> {
        co_await when_all(waiter(), setter());
    };

    auto [result] = run(combined());
    EXPECT(result.has_value());
    EXPECT(finished == 2);
}

// A sibling cancelled while it is between awaits stops at its next one. Were
// the cancellation lost, the loop would run all its iterations.
ZEST_CASE(any_cancel_stops_a_looping_sibling_at_its_next_await) {
    event ev;
    int iterations = 0;
    auto waiter = [&]() -> task<int> {
        co_await ev.wait();
        co_return 1;
    };
    auto looper = [&]() -> task<int> {
        for(int i = 0; i < 100; ++i) {
            co_await yield();
            iterations += 1;
            ev.set();
        }
        co_return 2;
    };
    auto combined = [&]() -> task<std::variant<int, int>> {
        co_return co_await when_any(waiter(), looper());
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(result->index() == 0U);
    EXPECT(iterations == 1);
}

ZEST_CASE(all_error_stops_a_looping_sibling_at_its_next_await) {
    event ev;
    int iterations = 0;
    auto failing = [&]() -> task<int, error> {
        co_await ev.wait();
        co_await fail(error::connection_refused);
    };
    auto looper = [&]() -> task<int, error> {
        for(int i = 0; i < 100; ++i) {
            co_await yield();
            iterations += 1;
            ev.set();
        }
        co_return 2;
    };
    auto combined = [&]() -> task<result<std::tuple<int, int>>> {
        co_return co_await when_all(failing(), looper());
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    ASSERT(result->has_error());
    EXPECT(result->error() == error::connection_refused);
    EXPECT(iterations == 1);
}

// A token that fires once when_all has settled finds nothing left to cancel.
ZEST_CASE(token_firing_after_all_settled_leaves_the_values) {
    event ev;
    cancellation_source source;
    auto request = [&]() -> task<int> {
        co_await ev.wait();
        co_return 42;
    };
    auto done = []() -> task<> {
        co_return;
    };
    bool settled = false;
    auto combined =
        [&]() -> task<outcome<std::tuple<int, int, std::nullopt_t>, void, cancellation>> {
        auto result = co_await when_all(with_token(request(), source.token()),
                                        with_token(request(), source.token()),
                                        done());
        settled = true;
        co_return result;
    };
    auto trigger = [&]() -> task<bool> {
        ev.set();
        co_await yield();
        bool settled_first = settled;
        source.cancel();
        co_return settled_first;
    };

    auto [result, settled_first] = run(combined(), trigger());
    ASSERT(settled_first.has_value());
    EXPECT(*settled_first);
    ASSERT(result.has_value());
    ASSERT(result->has_value());
    EXPECT(std::get<0>(**result) == 42);
    EXPECT(std::get<1>(**result) == 42);
}

// The three cases below are repros of a bookkeeping bug: when_all with a
// child that completes while the aggregate is being armed, next to two
// with_token children whose token and event fire in various orders, once
// read the cancelling child's index before it was recorded.

ZEST_CASE(token_firing_before_the_event_cancels_all) {
    event ev;
    cancellation_source source;
    auto request = [&]() -> task<int> {
        co_await ev.wait();
        co_return 42;
    };
    auto done = []() -> task<> {
        co_return;
    };
    auto combined = [&]() -> task<bool> {
        auto result = co_await when_all(with_token(request(), source.token()),
                                        with_token(request(), source.token()),
                                        done());
        co_return result.is_cancelled();
    };
    auto trigger = [&]() -> task<> {
        source.cancel();
        co_return;
    };

    auto [result, drove] = run(combined(), trigger());
    ASSERT(result.has_value());
    EXPECT(*result);
}

// The token fires before the event, both while the aggregate is armed, so
// the token wake-ups are queued ahead of the requests'.
ZEST_CASE(token_then_event_while_armed_cancels_all) {
    event ev;
    cancellation_source source;
    auto request = [&]() -> task<int> {
        co_await ev.wait();
        co_return 42;
    };
    auto fire = [&]() -> task<> {
        source.cancel();
        ev.set();
        co_return;
    };
    auto combined = [&]() -> task<bool> {
        auto result = co_await when_all(with_token(request(), source.token()),
                                        with_token(request(), source.token()),
                                        fire());
        co_return result.is_cancelled();
    };

    auto [result] = run(combined());
    ASSERT(result.has_value());
    EXPECT(*result);
}

// One request cancels the shared token as it completes; the cascade reaches
// the other with_token child through the outer when_all.
ZEST_CASE(child_firing_the_shared_token_cancels_all) {
    event ev;
    event never;
    cancellation_source source;
    auto cancels = [&]() -> task<int> {
        co_await ev.wait();
        source.cancel();
        co_return 42;
    };
    auto waits = [&]() -> task<int> {
        co_await never.wait();
        co_return 99;
    };
    auto done = []() -> task<> {
        co_return;
    };
    auto combined = [&]() -> task<bool> {
        auto result = co_await when_all(with_token(waits(), source.token()),
                                        with_token(cancels(), source.token()),
                                        done());
        co_return result.is_cancelled();
    };
    auto trigger = [&]() -> task<> {
        ev.set();
        co_return;
    };

    auto [result, drove] = run(combined(), trigger());
    ASSERT(result.has_value());
    EXPECT(*result);
    EXPECT(never.get_head() == nullptr);
}

};  // ZEST_SUITE(async_runtime_when_reentrancy)

}  // namespace

}  // namespace kota
