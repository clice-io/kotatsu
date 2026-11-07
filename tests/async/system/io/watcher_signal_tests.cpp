#include <csignal>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

#include "async/harness/io.h"
#include "sys/harness/signals.h"
#include "kotatsu/zest/async.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/async/async.h"

namespace kotatsu::async {

namespace {

// Raising a signal to this process and catching it back is POSIX only, as
// are the actions a signal is left with.
#ifndef _WIN32

/// How the action of `signum` stands: "default", "ignored", or "caught" by a
/// handler.
std::string_view action_of(int signum) {
    struct sigaction action = {};
    ::sigaction(signum, nullptr, &action);
    if(action.sa_handler == SIG_DFL) {
        return "default";
    }
    return action.sa_handler == SIG_IGN ? "ignored" : "caught";
}

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

ZEST_CASE(leaving_sigchld_ignored_fails) {
    test::ScopedSignalAction found(SIGCHLD, SIG_DFL);
    auto sig = signal::create(loop);
    ZASSERT(sig.has_value());
    ZEXPECT(sig->start(SIGCHLD, signal::leave::ignored) == error::invalid_argument);
    ZEXPECT(!sig->start(SIGCHLD));
    ZEXPECT(sig->start(SIGCHLD, signal::leave::ignored) == error::invalid_argument);
    ZEXPECT(!sig->stop());
    ZEXPECT(action_of(SIGCHLD) == "default");
}

// Each case below sets the actions it looks at, and puts them back as it
// found them when it ends.

// libuv sets the default action, not the one the signal had before: the
// signal would end the process now, which the test only looks at.
ZEST_CASE(stop_leaves_the_default_action) {
    test::ScopedSignalAction found(SIGUSR1, SIG_IGN);
    auto sig = signal::create(loop);
    ZASSERT(sig.has_value());
    ZASSERT(!sig->start(SIGUSR1));
    ZEXPECT(action_of(SIGUSR1) == "caught");

    ZEXPECT(!sig->stop());
    ZEXPECT(action_of(SIGUSR1) == "default");
}

ZEST_CASE(stop_leaves_the_signal_ignored_when_asked) {
    test::ScopedSignalAction found(SIGUSR1, SIG_DFL);
    auto sig = signal::create(loop);
    ZASSERT(sig.has_value());
    ZASSERT(!sig->start(SIGUSR1, signal::leave::ignored));

    ZEXPECT(!sig->stop());
    // The default action would end the process.
    ZASSERT(action_of(SIGUSR1) == "ignored");
    ZEXPECT(::raise(SIGUSR1) == 0);
}

ZEST_CASE(destruction_leaves_the_signal_as_asked) {
    test::ScopedSignalAction found(SIGUSR1, SIG_DFL);
    auto created = signal::create(loop);
    ZASSERT(created.has_value());
    std::optional<signal> sig = std::move(*created);
    ZASSERT(!sig->start(SIGUSR1, signal::leave::ignored));

    sig.reset();
    ZEXPECT(action_of(SIGUSR1) == "ignored");
}

// A switch to another signal stops watching the first.
ZEST_CASE(start_on_another_signal_leaves_the_first_as_asked) {
    test::ScopedSignalAction found_first(SIGUSR1, SIG_DFL);
    test::ScopedSignalAction found_second(SIGUSR2, SIG_DFL);
    auto sig = signal::create(loop);
    ZASSERT(sig.has_value());
    ZASSERT(!sig->start(SIGUSR1, signal::leave::ignored));

    ZASSERT(!sig->start(SIGUSR2));
    ZEXPECT(action_of(SIGUSR1) == "ignored");
    ZEXPECT(action_of(SIGUSR2) == "caught");
    ZEXPECT(!sig->stop());
    ZEXPECT(action_of(SIGUSR2) == "default");
}

// The first to stop leaves the signal watched by the second, whatever it
// was to leave it as: the second still catches it, and what it leaves it as
// counts.
ZEST_CASE(stop_of_one_of_two_watchers_leaves_the_signal_watched) {
    test::ScopedSignalAction found(SIGUSR1, SIG_DFL);
    auto first = signal::create(loop);
    auto second = signal::create(loop);
    ZASSERT(first.has_value());
    ZASSERT(second.has_value());
    ZASSERT(!first->start(SIGUSR1, signal::leave::ignored));
    ZASSERT(!second->start(SIGUSR1));

    ZEXPECT(!first->stop());
    // The default action would end the process.
    ZASSERT(action_of(SIGUSR1) == "caught");
    auto wait_one = [&]() -> task<void, error> {
        ::raise(SIGUSR1);
        co_await second->wait().or_fail();
    };
    auto [caught] = run(wait_one());
    ZEXPECT(caught.has_value());
    ZEXPECT(!second->stop());
    ZEXPECT(action_of(SIGUSR1) == "default");
}

// A fire kept from before stays across a start() on the signal watched
// already, which changes only what the signal is left as. The loop reads the
// raised signal on its way to the yield's turn.
ZEST_CASE(start_on_the_same_signal_changes_only_what_it_is_left_as) {
    test::ScopedSignalAction found(SIGUSR1, SIG_DFL);
    auto sig = signal::create(loop);
    ZASSERT(sig.has_value());
    ZASSERT(!sig->start(SIGUSR1));
    auto fire_then_start = [&]() -> task<std::size_t, error> {
        ::raise(SIGUSR1);
        co_await yield();
        ZEXPECT(!sig->start(SIGUSR1, signal::leave::ignored));
        co_return co_await test::winner(sig->wait(), yield()).or_fail();
    };

    auto [kept] = run(fire_then_start());
    ZASSERT(kept.has_value());
    ZEXPECT(*kept == 0U);
    ZEXPECT(!sig->stop());
    ZEXPECT(action_of(SIGUSR1) == "ignored");
}

// libuv sets its handler when a watcher starts on a signal none watches,
// whatever the signal's action was.
ZEST_CASE(start_catches_a_signal_left_ignored) {
    test::ScopedSignalAction found(SIGUSR1, SIG_IGN);
    auto sig = signal::create(loop);
    ZASSERT(sig.has_value());
    ZASSERT(!sig->start(SIGUSR1));
    auto wait_one = [&]() -> task<void, error> {
        ::raise(SIGUSR1);
        co_await sig->wait().or_fail();
    };

    auto [caught] = run(wait_one());
    ZEXPECT(caught.has_value());
    ZEXPECT(!sig->stop());
}

// libuv stops watching the first signal before it finds that the number
// names none; the fire kept from the first goes with it. The loop reads the
// raised signal on its way to the yield's turn.
ZEST_CASE(failed_start_on_another_signal_leaves_the_first_as_asked) {
    test::ScopedSignalAction found(SIGUSR1, SIG_DFL);
    auto sig = signal::create(loop);
    ZASSERT(sig.has_value());
    ZASSERT(!sig->start(SIGUSR1, signal::leave::ignored));
    auto fire_then_fail = [&]() -> task<std::size_t, error> {
        ::raise(SIGUSR1);
        co_await yield();
        ZEXPECT(sig->start(-1) == error::invalid_argument);
        co_return co_await test::winner(sig->wait(), yield()).or_fail();
    };

    auto [dropped] = run(fire_then_fail());
    ZASSERT(dropped.has_value());
    ZEXPECT(*dropped == 1U);
    ZEXPECT(action_of(SIGUSR1) == "ignored");
}

// The loop's destruction closes the watcher's handle, which libuv stops
// without the watcher: the default action stays, also once the watcher goes.
ZEST_CASE(destroying_the_loop_leaves_the_default_action) {
    test::ScopedSignalAction found(SIGUSR1, SIG_IGN);
    std::optional<signal> sig;
    {
        event_loop own;
        auto created = signal::create(own);
        ZASSERT(created.has_value());
        sig.emplace(std::move(*created));
        ZASSERT(!sig->start(SIGUSR1, signal::leave::ignored));
    }
    ZEXPECT(action_of(SIGUSR1) == "default");

    sig.reset();
    ZEXPECT(action_of(SIGUSR1) == "default");
}

};  // ZEST_SUITE(async_io_watcher_signal)

#endif  // !_WIN32

}  // namespace

}  // namespace kotatsu::async
