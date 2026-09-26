#include <csignal>
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

ZEST_CASE(second_wait_while_one_is_pending_fails) {
    auto sig = signal::create(loop);
    ASSERT(sig.has_value());
    ASSERT(!sig->start(SIGUSR1));
    auto first = sig->wait();
    auto* node = first.operator->();
    auto second_then_cancel = [&]() -> task<result<void>> {
        auto second = co_await sig->wait();
        node->cancel();
        co_return second;
    };

    auto [pending, second] = run(std::move(first), second_then_cancel());
    EXPECT(pending.is_cancelled());
    ASSERT(second.has_value());
    ASSERT(second->has_error());
    EXPECT(second->error() == error::connection_already_in_progress);
    EXPECT(!sig->stop());
}

ZEST_CASE(wait_can_be_cancelled) {
    auto sig = signal::create(loop);
    ASSERT(sig.has_value());
    ASSERT(!sig->start(SIGUSR1));
    auto waiting = sig->wait();
    auto* node = waiting.operator->();
    auto cancel_it = [&]() -> task<> {
        node->cancel();
        co_return;
    };

    auto [cancelled, driver] = run(std::move(waiting), cancel_it());
    EXPECT(cancelled.is_cancelled());
    EXPECT(!sig->stop());
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
