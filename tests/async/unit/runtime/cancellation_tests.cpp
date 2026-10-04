#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

task<int> ready(int value) {
    co_return value;
}

ZEST_SUITE(async_runtime_cancellation, test::LoopFixture) {

ZEST_CASE(cancel_reaches_every_token) {
    cancellation_source source;
    auto token = source.token();
    auto copy = token;
    EXPECT(!source.cancelled());
    EXPECT(!token.cancelled());

    source.cancel();
    source.cancel();
    EXPECT(source.cancelled());
    EXPECT(token.cancelled());
    EXPECT(copy.cancelled());
}

ZEST_CASE(destroying_the_source_cancels_its_tokens) {
    std::optional<cancellation_source> source(std::in_place);
    auto token = source->token();
    source.reset();
    EXPECT(token.cancelled());
}

ZEST_CASE(token_wait_ends_cancelled_when_the_source_fires) {
    cancellation_source source;
    auto fire = [&]() -> task<> {
        source.cancel();
        co_return;
    };

    auto [waiter, driver] = run(source.token().wait(), fire());
    EXPECT(waiter.is_cancelled());
    EXPECT(driver.has_value());
}

ZEST_CASE(token_wait_on_a_fired_token_cancels_at_once) {
    cancellation_source source;
    source.cancel();

    auto [waiter] = run(source.token().wait());
    EXPECT(waiter.is_cancelled());
}

ZEST_CASE(with_token_passes_the_value_through) {
    cancellation_source first;
    cancellation_source second;

    auto [one, two] = run(with_token(ready(42), first.token()),
                          with_token(ready(7), first.token(), second.token()));
    ASSERT(one.has_value());
    EXPECT(*one == 42);
    ASSERT(two.has_value());
    EXPECT(*two == 7);
}

ZEST_CASE(with_token_passes_the_error_through) {
    cancellation_source source;
    auto failing = []() -> task<int, error> {
        co_await fail(error::connection_refused);
    };

    auto [guarded] = run(with_token(failing(), source.token()));
    ASSERT(guarded.has_error());
    EXPECT(guarded.error() == error::connection_refused);
}

ZEST_CASE(with_token_on_a_fired_token_never_starts_the_task) {
    cancellation_source first;
    cancellation_source second;
    second.cancel();
    int started = 0;
    auto worker = [&]() -> task<int> {
        started += 1;
        co_return 1;
    };

    auto [single, several] = run(with_token(worker(), second.token()),
                                 with_token(worker(), first.token(), second.token()));
    EXPECT(single.is_cancelled());
    EXPECT(several.is_cancelled());
    EXPECT(started == 0);
}

ZEST_CASE(with_token_cancels_the_task_in_flight) {
    cancellation_source source;
    event gate;
    int started = 0;
    auto worker = [&]() -> task<int, error> {
        started += 1;
        co_await gate.wait();
        co_return 1;
    };
    auto fire = [&]() -> task<> {
        source.cancel();
        co_return;
    };

    auto [guarded, driver] = run(with_token(worker(), source.token()), fire());
    EXPECT(guarded.is_cancelled());
    EXPECT(started == 1);
    EXPECT(!gate.has_waiters());
    EXPECT(driver.has_value());
}

// MSVC's coroutine codegen once fell through the cancelled path of the void
// specialization and dereferenced the cancelled race result.
ZEST_CASE(with_token_cancels_a_void_task_in_flight) {
    cancellation_source source;
    event gate;
    bool started = false;
    auto worker = [&]() -> task<> {
        started = true;
        co_await gate.wait();
    };
    auto fire = [&]() -> task<> {
        source.cancel();
        co_return;
    };

    auto [guarded, driver] = run(with_token(worker(), source.token()), fire());
    EXPECT(guarded.is_cancelled());
    EXPECT(started);
    EXPECT(!gate.has_waiters());
    EXPECT(driver.has_value());
}

ZEST_CASE(with_token_cancels_on_any_of_its_tokens) {
    for(int fired: {0, 1}) {
        ZEST_CONTEXT("source {} fires", fired);
        cancellation_source sources[2];
        event gate;
        bool started = false;
        auto worker = [&]() -> task<int> {
            started = true;
            co_await gate.wait();
            co_return 1;
        };
        auto fire = [&]() -> task<> {
            sources[fired].cancel();
            co_return;
        };

        auto [guarded, driver] =
            run(with_token(worker(), sources[0].token(), sources[1].token()), fire());
        EXPECT(guarded.is_cancelled());
        EXPECT(started);
        // The cancel reached the worker's wait, not only the wrapper.
        EXPECT(!gate.has_waiters());
        EXPECT(driver.has_value());
    }
}

ZEST_CASE(one_token_cancels_every_task_it_guards) {
    cancellation_source source;
    auto token = source.token();
    event gates[3];
    int started = 0;
    auto worker = [&](event& gate) -> task<int> {
        started += 1;
        co_await gate.wait();
        co_return 1;
    };
    auto fire = [&]() -> task<> {
        source.cancel();
        co_return;
    };

    auto [first, second, third, driver] = run(with_token(worker(gates[0]), token),
                                              with_token(worker(gates[1]), token),
                                              with_token(worker(gates[2]), token),
                                              fire());
    EXPECT(first.is_cancelled());
    EXPECT(second.is_cancelled());
    EXPECT(third.is_cancelled());
    EXPECT(started == 3);
    for(auto& gate: gates) {
        EXPECT(!gate.has_waiters());
    }
    EXPECT(driver.has_value());
}

ZEST_CASE(outer_token_cancels_a_nested_with_token) {
    cancellation_source outer;
    cancellation_source inner;
    event gate;
    bool started = false;
    auto worker = [&]() -> task<int> {
        started = true;
        co_await gate.wait();
        co_return 42;
    };
    auto fire = [&]() -> task<> {
        outer.cancel();
        co_return;
    };

    auto [guarded, driver] =
        run(with_token(with_token(worker(), inner.token()), outer.token()), fire());
    EXPECT(guarded.is_cancelled());
    EXPECT(started);
    // The cancel went through the inner wrapper to the worker's wait.
    EXPECT(!gate.has_waiters());
    EXPECT(driver.has_value());
}

ZEST_CASE(inner_token_cancel_reaches_the_outer_as_cancellation) {
    cancellation_source outer;
    cancellation_source inner;
    event gate;
    bool started = false;
    auto worker = [&]() -> task<int> {
        started = true;
        co_await gate.wait();
        co_return 42;
    };
    auto fire = [&]() -> task<> {
        inner.cancel();
        co_return;
    };

    auto [guarded, driver] =
        run(with_token(with_token(worker(), inner.token()), outer.token()), fire());
    EXPECT(guarded.is_cancelled());
    EXPECT(started);
    EXPECT(!gate.has_waiters());
    EXPECT(driver.has_value());
}

ZEST_CASE(nested_with_token_sharing_one_token_cancels_the_inner_task) {
    cancellation_source source;
    auto token = source.token();
    event gate;
    event inner_started;
    bool inner_cancelled = false;
    auto inner = [&]() -> task<> {
        inner_started.set();
        co_await gate.wait();
    };
    auto outer = [&]() -> task<> {
        auto result = co_await with_token(inner(), token);
        inner_cancelled = result.is_cancelled();
    };
    auto fire = [&]() -> task<> {
        co_await inner_started.wait();
        source.cancel();
    };

    auto [guarded, driver] = run(with_token(outer(), token), fire());
    EXPECT(inner_cancelled);
    EXPECT(driver.has_value());
}

ZEST_CASE(default_token_never_fires) {
    cancellation_token token;
    bool called = false;
    auto registration = token.on_cancel([&] { called = true; });

    auto [guarded] = run(with_token(ready(3), token));
    EXPECT(!token.cancelled());
    ASSERT(guarded.has_value());
    EXPECT(*guarded == 3);
    EXPECT(!called);
}

// A wait on a token without a source ends only when it is cancelled.
ZEST_CASE(default_token_wait_ends_when_cancelled) {
    cancellation_token token;
    auto waiting = token.wait();
    auto canceller = [&]() -> task<> {
        co_await yield();
        waiting.cancel();
    };

    auto [waited, cancelled] = run(waiting, canceller());
    EXPECT(waited.is_cancelled());
    EXPECT(cancelled.has_value());
}

ZEST_CASE(on_cancel_runs_inside_cancel_in_registration_order) {
    cancellation_source source;
    auto token = source.token();
    std::vector<std::string> order;
    bool seen_cancelled = false;
    auto first = token.on_cancel([&] {
        seen_cancelled = token.cancelled();
        order.emplace_back("first");
    });
    auto second = token.on_cancel([&] { order.emplace_back("second"); });

    source.cancel();
    order.emplace_back("returned");
    source.cancel();
    EXPECT(seen_cancelled);
    EXPECT(order == std::vector<std::string>{"first", "second", "returned"});
}

ZEST_CASE(on_cancel_of_a_fired_token_runs_at_once) {
    cancellation_source source;
    source.cancel();
    int calls = 0;
    auto registration = source.token().on_cancel([&] { calls += 1; });
    EXPECT(calls == 1);
}

ZEST_CASE(destroying_the_source_runs_the_callbacks) {
    std::optional<cancellation_source> source(std::in_place);
    int calls = 0;
    auto registration = source->token().on_cancel([&] { calls += 1; });
    source.reset();
    EXPECT(calls == 1);
}

ZEST_CASE(destroyed_registration_never_runs) {
    cancellation_source source;
    int calls = 0;
    {
        auto registration = source.token().on_cancel([&] { calls += 1; });
    }
    source.cancel();
    EXPECT(calls == 0);
}

ZEST_CASE(moved_registration_runs_once) {
    cancellation_source source;
    int calls = 0;
    auto registration = source.token().on_cancel([&] { calls += 1; });
    auto moved = std::move(registration);
    cancellation_callback assigned;
    assigned = std::move(moved);
    source.cancel();
    EXPECT(calls == 1);
}

ZEST_CASE(assigning_a_registration_deregisters_the_one_it_held) {
    cancellation_source source;
    int replaced = 0;
    int kept = 0;
    auto registration = source.token().on_cancel([&] { replaced += 1; });
    registration = source.token().on_cancel([&] { kept += 1; });
    source.cancel();
    EXPECT(replaced == 0);
    EXPECT(kept == 1);
}

ZEST_CASE(callback_deregistering_a_later_one_keeps_it_from_running) {
    cancellation_source source;
    auto token = source.token();
    int later_calls = 0;
    std::optional<cancellation_callback> later;
    auto first = token.on_cancel([&] { later.reset(); });
    later.emplace(token.on_cancel([&] { later_calls += 1; }));

    source.cancel();
    EXPECT(later_calls == 0);
}

ZEST_CASE(callback_may_destroy_its_own_registration) {
    cancellation_source source;
    auto token = source.token();
    std::optional<cancellation_callback> own;
    int calls = 0;
    own.emplace(token.on_cancel([&] {
        calls += 1;
        own.reset();
    }));

    source.cancel();
    EXPECT(calls == 1);
    EXPECT(!own.has_value());
}

ZEST_CASE(callback_registering_another_runs_it_at_once) {
    cancellation_source source;
    auto token = source.token();
    std::vector<std::string> order;
    cancellation_callback nested;
    auto outer = token.on_cancel([&] {
        nested = token.on_cancel([&] { order.emplace_back("nested"); });
        order.emplace_back("outer");
    });

    source.cancel();
    EXPECT(order == std::vector<std::string>{"nested", "outer"});
}

ZEST_CASE(callback_may_destroy_the_source) {
    auto source = std::make_unique<cancellation_source>();
    auto token = source->token();
    int later_calls = 0;
    auto first = token.on_cancel([&] { source.reset(); });
    auto later = token.on_cancel([&] { later_calls += 1; });

    source->cancel();
    EXPECT(source == nullptr);
    EXPECT(later_calls == 1);
}

// A registration may outlive its source, which ran the callback as it went:
// destroying the registration then does nothing more.
ZEST_CASE(registration_outlives_its_source) {
    int calls = 0;
    cancellation_callback registration;
    {
        cancellation_source source;
        registration = source.token().on_cancel([&] { calls += 1; });
    }
    EXPECT(calls == 1);
    registration = cancellation_callback();
    EXPECT(calls == 1);
}

// Callbacks run inside cancel(); the waits a token guards resume only after.
ZEST_CASE(on_cancel_runs_before_the_waits_resume) {
    cancellation_source source;
    auto token = source.token();
    std::vector<std::string> order;
    auto registration = token.on_cancel([&] { order.emplace_back("callback"); });
    auto waiter = [&]() -> task<> {
        auto waited = co_await token.wait().catch_cancel();
        if(waited.is_cancelled()) {
            order.emplace_back("wait");
        }
    };
    auto fire = [&]() -> task<> {
        co_await yield();
        source.cancel();
        order.emplace_back("cancelled");
    };

    auto [waited, fired] = run(waiter(), fire());
    EXPECT(waited.has_value());
    EXPECT(fired.has_value());
    EXPECT(order == std::vector<std::string>{"callback", "cancelled", "wait"});
}

// A cancel() made outside any task, here a relay's, runs every callback
// before the waits it woke resume, even when a callback cancels a task.
ZEST_CASE(on_cancel_outside_a_task_runs_every_callback_before_the_waits) {
    cancellation_source source;
    auto token = source.token();
    event gate;
    std::vector<std::string> order;
    auto waiting = [&]() -> task<> {
        co_await gate.wait();
    };
    auto guarded = [&]() -> task<> {
        auto result = co_await with_token(waiting(), token);
        if(result.is_cancelled()) {
            order.emplace_back("guarded");
        }
    };
    auto other = waiting();
    auto first = token.on_cancel([&] {
        other.cancel();
        order.emplace_back("first");
    });
    auto second = token.on_cancel([&] { order.emplace_back("second"); });
    auto relay = loop.create_relay();
    auto firer = [&]() -> task<> {
        co_await yield();
        relay.send([&] {
            source.cancel();
            order.emplace_back("returned");
        });
    };

    auto [guarded_result, other_result, fired] = run(guarded(), other, firer());
    EXPECT(guarded_result.has_value());
    EXPECT(other_result.is_cancelled());
    EXPECT(fired.has_value());
    EXPECT(order == std::vector<std::string>{"first", "second", "returned", "guarded"});
}

};  // ZEST_SUITE(async_runtime_cancellation)

}  // namespace

}  // namespace kota
