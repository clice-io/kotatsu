#pragma once

#include <chrono>

#include "kota/async/io/loop.h"
#include "kota/async/runtime/task.h"
#include "kota/async/vocab/error.h"
#include "kota/async/vocab/owned.h"

namespace kota {

/// What timer, signal, idle, prepare and check share: a libuv watcher that
/// fires once started, and the one task waiting for it.
///
/// wait() waits for the next fire. A fire nobody waited for is kept for the
/// next wait(): a signal counts every one, the others keep one, however
/// many happened. One wait() may be pending at a time; a second fails with
/// error::resource_busy_or_locked. Cancelling a wait only withdraws it: the
/// watcher runs on. Destroying the watcher ends a pending wait with
/// error::operation_aborted.
///
/// A default-constructed or moved-from watcher is inert: everything fails
/// with error::invalid_argument.
class watcher {
public:
    watcher() noexcept;

    watcher(const watcher&) = delete;
    watcher& operator=(const watcher&) = delete;

    watcher(watcher&& other) noexcept;
    watcher& operator=(watcher&& other) noexcept;

    ~watcher();

    /// Stops firing until the next start(); a pending wait() keeps waiting
    /// for that.
    error stop();

    /// Waits for the next fire.
    task<void, error> wait();

protected:
    struct Self;

    explicit watcher(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

/// Fires once `timeout` after start(), then every `repeat`, if not zero.
class timer : public watcher {
public:
    timer() noexcept = default;

    static timer create(event_loop& loop = event_loop::current());

    /// Starts the timer, or restarts it with the new times.
    error start(std::chrono::milliseconds timeout, std::chrono::milliseconds repeat = {});

private:
    using watcher::watcher;

    friend task<> sleep(std::chrono::milliseconds timeout, event_loop& loop);
};

/// Fires when the process receives a signal.
class signal : public watcher {
public:
    signal() noexcept = default;

    static result<signal> create(event_loop& loop = event_loop::current());

    /// Watches `signum`, or switches to it; fails with invalid_argument for
    /// a number that names no signal.
    error start(int signum);

private:
    using watcher::watcher;
};

/// Fires once on every loop iteration in which the loop does not block
/// waiting for I/O; a running idle watcher keeps it from blocking.
class idle : public watcher {
public:
    idle() noexcept = default;

    static idle create(event_loop& loop = event_loop::current());

    error start();

private:
    using watcher::watcher;
};

/// Fires once on every loop iteration, right before the loop polls for I/O.
class prepare : public watcher {
public:
    prepare() noexcept = default;

    static prepare create(event_loop& loop = event_loop::current());

    error start();

private:
    using watcher::watcher;
};

/// Fires once on every loop iteration, right after the loop polled for I/O.
class check : public watcher {
public:
    check() noexcept = default;

    static check create(event_loop& loop = event_loop::current());

    error start();

private:
    using watcher::watcher;
};

/// Resumes after `timeout`.
task<> sleep(std::chrono::milliseconds timeout, event_loop& loop = event_loop::current());

inline task<> sleep(int ms, event_loop& loop = event_loop::current()) {
    return sleep(std::chrono::milliseconds{ms}, loop);
}

}  // namespace kota
