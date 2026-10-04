#include <csignal>
#include <cstddef>
#include <optional>
#include <utility>

#include "async/harness/io.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

// Raising a signal to this process and catching it back is POSIX only.
#ifndef _WIN32

ZEST_SUITE(async_io_watcher_signal, zest::LoopFixture) {

ZEST_CASE(every_raised_signal_wakes_one_wait) {
    auto sig = signal::create(loop);
    ZASSERT(sig.has_value());
    ZASSERT(!sig->start(SIGUSR1));
    auto wait_twice = [&]() -> task<void, error> {
        ::raise(SIGUSR1);
        ::raise(SIGUSR1);
        co_await sig->wait().or_fail();
        co_await sig->wait().or_fail();
    };

    auto [result] = run(wait_twice());
    ZEXPECT(result.has_value());
    ZEXPECT(!sig->stop());
}

// A signal raised while nobody waits is kept for the next wait(), also across
// a start() on the signal watched already, and dropped by a start() that
// switches to another. The loop reads a raised signal on its way to the
// yield's turn.
ZEST_CASE(switching_to_another_signal_drops_the_kept_fires) {
    auto sig = signal::create(loop);
    ZASSERT(sig.has_value());
    ZASSERT(!sig->start(SIGUSR1));
    auto waiter = [&]() -> task<std::pair<std::size_t, std::size_t>, error> {
        ::raise(SIGUSR1);
        co_await yield();
        ZEXPECT(!sig->start(SIGUSR1));
        auto kept = co_await test::winner(sig->wait(), yield()).or_fail();
        ::raise(SIGUSR1);
        co_await yield();
        ZEXPECT(!sig->start(SIGUSR2));
        auto dropped = co_await test::winner(sig->wait(), yield()).or_fail();
        co_return std::pair{kept, dropped};
    };

    auto [result] = run(waiter());
    ZASSERT(result.has_value());
    ZEXPECT(result->first == 0U);
    ZEXPECT(result->second == 1U);
    ZEXPECT(!sig->stop());
}

// The first wait is withdrawn once the second has failed.
ZEST_CASE(second_wait_while_one_is_pending_fails) {
    auto sig = signal::create(loop);
    ZASSERT(sig.has_value());
    ZASSERT(!sig->start(SIGUSR1));
    auto wait_twice = [&]() -> task<error> {
        auto both = co_await when_any(sig->wait(), sig->wait());
        co_return both.has_error() ? both.error() : error();
    };

    auto [second] = run(wait_twice());
    ZASSERT(second.has_value());
    ZEXPECT(*second == error::resource_busy_or_locked);
    ZEXPECT(!sig->stop());
}

// Nothing raises the signal, so only the cancel can end the wait.
ZEST_CASE(wait_can_be_cancelled) {
    auto sig = signal::create(loop);
    ZASSERT(sig.has_value());
    ZASSERT(!sig->start(SIGUSR1));
    auto race = [&]() -> task<std::size_t, error> {
        auto first = co_await or_fail(co_await when_any(sig->wait(), yield()));
        co_return first.index();
    };

    auto [result] = run(race());
    ZASSERT(result.has_value());
    ZEXPECT(*result == 1U);
    ZEXPECT(!sig->stop());
}

// Were the signal no longer watched, SIGUSR1 would end the process.
ZEST_CASE(cancelled_wait_leaves_the_signal_watched) {
    auto sig = signal::create(loop);
    ZASSERT(sig.has_value());
    ZASSERT(!sig->start(SIGUSR1));
    auto waiter = [&]() -> task<std::size_t, error> {
        auto first = co_await or_fail(co_await when_any(sig->wait(), yield()));
        ::raise(SIGUSR1);
        co_await sig->wait().or_fail();
        co_return first.index();
    };

    auto [result] = run(waiter());
    ZASSERT(result.has_value());
    ZEXPECT(*result == 1U);
    ZEXPECT(!sig->stop());
}

ZEST_CASE(wait_ended_by_destroying_its_signal_fails) {
    auto created = signal::create(loop);
    ZASSERT(created.has_value());
    ZASSERT(!created->start(SIGUSR1));
    std::optional<signal> sig = std::move(*created);
    auto destroy = [&]() -> task<> {
        sig.reset();
        co_return;
    };

    auto [waited, destroyed] = run(sig->wait(), destroy());
    ZASSERT(waited.has_error());
    ZEXPECT(waited.error() == error::operation_aborted);
}

ZEST_CASE(start_with_an_invalid_signal_fails) {
    auto sig = signal::create(loop);
    ZASSERT(sig.has_value());
    ZEXPECT(sig->start(0) == error::invalid_argument);
}

};  // ZEST_SUITE(async_io_watcher_signal)

#endif  // !_WIN32

}  // namespace

}  // namespace kota
