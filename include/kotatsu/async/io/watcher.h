#pragma once

#include <chrono>
#include <cstdint>

#include "kotatsu/async/io/loop.h"
#include "kotatsu/async/runtime/task.h"
#include "kotatsu/async/runtime/when.h"
#include "kotatsu/async/vocab/error.h"
#include "kotatsu/async/vocab/owned.h"

namespace kotatsu::async {

/// What timer, signal, idle, prepare and check share: a libuv watcher that
/// fires once started, and the one task waiting for it.
///
/// wait() waits for the next fire. A fire nobody waited for is kept for the
/// next wait(): a signal counts every one, the others keep one, however
/// many happened; a timer drops it when it is started again, and a signal
/// when it is started on a signal it does not watch. One wait() may be
/// pending at a time; a second fails with
/// error::resource_busy_or_locked. Cancelling a wait only withdraws it: the
/// watcher runs on. Destroying the watcher ends a pending wait with
/// error::operation_aborted.
///
/// A default-constructed or moved-from watcher is inert: what can fail fails
/// with error::invalid_argument.
class watcher {
public:
    watcher() noexcept;

    watcher(const watcher&) = delete;
    watcher& operator=(const watcher&) = delete;

    watcher(watcher&& other) noexcept;
    watcher& operator=(watcher&& other) noexcept;

    ~watcher();

    /// Stops firing until the next start(), and ends a pending wait() with
    /// error::operation_aborted. A fire kept for the next wait() stays.
    error stop();

    /// Waits for the next fire.
    task<void, error> wait();

protected:
    struct Self;

    explicit watcher(detail::unique_handle<Self> self) noexcept;

    /// Starts an idle, prepare or check watcher.
    error start();

    detail::unique_handle<Self> self;
};

/// Fires once `timeout` after start(), then every `repeat`, if not zero.
class timer : public watcher {
public:
    timer() noexcept = default;

    static timer create(event_loop& loop = event_loop::current());

    /// Starts the timer, or restarts it with the new times; a fire kept from
    /// before is dropped.
    error start(std::chrono::milliseconds timeout, std::chrono::milliseconds repeat = {});

private:
    using watcher::watcher;

    friend task<> sleep(std::chrono::milliseconds timeout, event_loop& loop);
};

/// Fires when the process receives a signal.
///
/// Once no signal watcher in the process watches a signal any more, after a
/// stop(), a start() on another signal or a destruction, libuv sets its
/// action to the default (SIG_DFL), not to the action it had before the
/// first start(): an ignored SIGPIPE, watched and stopped, ends the process
/// again. The watcher that stopped last leaves the signal as its start()'s
/// `then` says; one its loop's destruction stopped leaves the default. A
/// start() on a signal no watcher watches catches it again, whatever its
/// action.
///
/// On Windows libuv makes SIGINT, SIGBREAK, SIGHUP and SIGWINCH of console
/// events, and there is no action to leave: once the last watcher of SIGINT
/// or SIGBREAK stops, Ctrl+C or Ctrl+Break ends the process again.
class signal : public watcher {
public:
    /// What a signal is left as once no watcher watches it.
    enum class leave : std::uint8_t {
        /// The default action, which for most signals ends the process.
        default_action,
        /// Ignored (SIG_IGN), set just after libuv set the default action: a
        /// signal that arrives in between takes the default action. On
        /// Windows it is as default_action.
        ignored,
    };

    signal() noexcept = default;

    static result<signal> create(event_loop& loop = event_loop::current());

    /// Watches `signum`, or switches to it, and leaves it as `then` says if
    /// this watcher is the last to stop watching it; fails with
    /// invalid_argument for a number that names no signal, on POSIX having
    /// stopped watching the old signal unless the number is 0. The fires
    /// kept are dropped unless it watches `signum` already: then only `then`
    /// changes, as when a program decides, once the signal came, to leave
    /// it ignored.
    error start(int signum, leave then = leave::default_action);

private:
    using watcher::watcher;
};

/// Fires once on every loop iteration in which the loop does not block
/// waiting for I/O; a running idle watcher keeps it from blocking.
class idle : public watcher {
public:
    idle() noexcept = default;

    static idle create(event_loop& loop = event_loop::current());

    using watcher::start;

private:
    using watcher::watcher;
};

/// Fires once on every loop iteration, right before the loop polls for I/O.
class prepare : public watcher {
public:
    prepare() noexcept = default;

    static prepare create(event_loop& loop = event_loop::current());

    using watcher::start;

private:
    using watcher::watcher;
};

/// Fires once on every loop iteration, right after the loop polled for I/O.
class check : public watcher {
public:
    check() noexcept = default;

    static check create(event_loop& loop = event_loop::current());

    using watcher::start;

private:
    using watcher::watcher;
};

/// Resumes after `timeout`.
task<> sleep(std::chrono::milliseconds timeout, event_loop& loop = event_loop::current());

inline task<> sleep(int ms, event_loop& loop = event_loop::current()) {
    return sleep(std::chrono::milliseconds{ms}, loop);
}

namespace detail {

/// Ends cancelled once `timeout` has passed: it never succeeds.
task<> expire(std::chrono::milliseconds timeout, event_loop& loop);

}  // namespace detail

/// Runs `inner_task`, cancelling it once `timeout` has passed; one not above
/// zero passes on the loop's next turn. A task that takes its time to end
/// once cancelled still ends first: this one ends with it. The result reports
/// that cancellation, or one of the task itself, as a value.
template <typename T, typename E, typename C>
task<T, E, cancellation> with_timeout(task<T, E, C> inner_task,
                                      std::chrono::milliseconds timeout,
                                      event_loop& loop = event_loop::current()) {
    return detail::run_until(std::move(inner_task), detail::expire(timeout, loop));
}

}  // namespace kotatsu::async
