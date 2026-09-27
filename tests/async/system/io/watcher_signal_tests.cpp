#include <csignal>
#include <cstddef>
#include <optional>
#include <utility>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

// Raising a signal to this process and catching it back is POSIX only.
#ifndef _WIN32

ZEST_SUITE(async_io_watcher_signal, test::LoopFixture) {

ZEST_CASE(every_raised_signal_wakes_one_wait) {
    auto sig = signal::create(loop);
    ASSERT(sig.has_value());
    ASSERT(!sig->start(SIGUSR1));
    auto wait_twice = [&]() -> task<void, error> {
        ::raise(SIGUSR1);
        ::raise(SIGUSR1);
        co_await sig->wait().or_fail();
        co_await sig->wait().or_fail();
    };

    auto [result] = run(wait_twice());
    EXPECT(result.has_value());
    EXPECT(!sig->stop());
}

// The first wait is withdrawn once the second has failed.
ZEST_CASE(second_wait_while_one_is_pending_fails) {
    auto sig = signal::create(loop);
    ASSERT(sig.has_value());
    ASSERT(!sig->start(SIGUSR1));
    auto wait_twice = [&]() -> task<error> {
        auto both = co_await when_any(sig->wait(), sig->wait());
        co_return both.has_error() ? both.error() : error();
    };

    auto [second] = run(wait_twice());
    ASSERT(second.has_value());
    EXPECT(*second == error::resource_busy_or_locked);
    EXPECT(!sig->stop());
}

// Nothing raises the signal, so only the cancel can end the wait.
ZEST_CASE(wait_can_be_cancelled) {
    auto sig = signal::create(loop);
    ASSERT(sig.has_value());
    ASSERT(!sig->start(SIGUSR1));
    auto race = [&]() -> task<std::size_t, error> {
        auto first = co_await or_fail(co_await when_any(sig->wait(), yield()));
        co_return first.index();
    };

    auto [result] = run(race());
    ASSERT(result.has_value());
    EXPECT(*result == 1U);
    EXPECT(!sig->stop());
}

// Were the signal no longer watched, SIGUSR1 would end the process.
ZEST_CASE(cancelled_wait_leaves_the_signal_watched) {
    auto sig = signal::create(loop);
    ASSERT(sig.has_value());
    ASSERT(!sig->start(SIGUSR1));
    auto waiter = [&]() -> task<std::size_t, error> {
        auto first = co_await or_fail(co_await when_any(sig->wait(), yield()));
        ::raise(SIGUSR1);
        co_await sig->wait().or_fail();
        co_return first.index();
    };

    auto [result] = run(waiter());
    ASSERT(result.has_value());
    EXPECT(*result == 1U);
    EXPECT(!sig->stop());
}

ZEST_CASE(destroying_a_signal_ends_its_wait) {
    auto created = signal::create(loop);
    ASSERT(created.has_value());
    ASSERT(!created->start(SIGUSR1));
    std::optional<signal> sig = std::move(*created);
    auto destroy = [&]() -> task<> {
        sig.reset();
        co_return;
    };

    auto [waited, destroyed] = run(sig->wait(), destroy());
    ASSERT(waited.has_error());
    EXPECT(waited.error() == error::operation_aborted);
}

ZEST_CASE(start_with_an_invalid_signal_fails) {
    auto sig = signal::create(loop);
    ASSERT(sig.has_value());
    EXPECT(sig->start(0) == error::invalid_argument);
}

};  // ZEST_SUITE(async_io_watcher_signal)

#endif  // !_WIN32

}  // namespace

}  // namespace kota
