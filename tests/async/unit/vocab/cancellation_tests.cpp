#include <optional>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

task<int> ready(int value) {
    co_return value;
}

ZEST_SUITE(async_vocab_cancellation, test::LoopFixture) {

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
    EXPECT(gate.get_head() == nullptr);
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
    EXPECT(gate.get_head() == nullptr);
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
        EXPECT(gate.get_head() == nullptr);
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
        EXPECT(gate.get_head() == nullptr);
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
    EXPECT(gate.get_head() == nullptr);
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
    EXPECT(gate.get_head() == nullptr);
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

};  // ZEST_SUITE(async_vocab_cancellation)

}  // namespace

}  // namespace kota
