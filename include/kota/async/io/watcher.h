#pragma once

#include <chrono>

#include "kota/async/runtime/task.h"
#include "kota/async/vocab/error.h"
#include "kota/async/vocab/owned.h"

namespace kota {

class event_loop;

class timer {
public:
    timer() noexcept;

    timer(const timer&) = delete;
    timer& operator=(const timer&) = delete;

    timer(timer&& other) noexcept;
    timer& operator=(timer&& other) noexcept;

    ~timer();

    struct Self;
    Self* operator->() noexcept;

    static timer create(event_loop& loop = event_loop::current());

    void start(std::chrono::milliseconds timeout, std::chrono::milliseconds repeat = {});

    void stop();

    task<> wait();

private:
    explicit timer(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

class signal {
public:
    signal() noexcept;

    signal(const signal&) = delete;
    signal& operator=(const signal&) = delete;

    signal(signal&& other) noexcept;
    signal& operator=(signal&& other) noexcept;

    ~signal();

    struct Self;
    Self* operator->() noexcept;

    static result<signal> create(event_loop& loop = event_loop::current());

    error start(int signum);

    error stop();

    task<void, error> wait();

private:
    explicit signal(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

class idle {
public:
    idle() noexcept;

    idle(const idle&) = delete;
    idle& operator=(const idle&) = delete;

    idle(idle&& other) noexcept;
    idle& operator=(idle&& other) noexcept;

    ~idle();

    struct Self;
    Self* operator->() noexcept;

    static idle create(event_loop& loop = event_loop::current());

    void start();

    void stop();

    task<> wait();

private:
    explicit idle(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

class prepare {
public:
    prepare() noexcept;

    prepare(const prepare&) = delete;
    prepare& operator=(const prepare&) = delete;

    prepare(prepare&& other) noexcept;
    prepare& operator=(prepare&& other) noexcept;

    ~prepare();

    struct Self;
    Self* operator->() noexcept;

    static prepare create(event_loop& loop = event_loop::current());

    void start();

    void stop();

    task<> wait();

private:
    explicit prepare(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

class check {
public:
    check() noexcept;

    check(const check&) = delete;
    check& operator=(const check&) = delete;

    check(check&& other) noexcept;
    check& operator=(check&& other) noexcept;

    ~check();

    struct Self;
    Self* operator->() noexcept;

    static check create(event_loop& loop = event_loop::current());

    void start();

    void stop();

    task<> wait();

private:
    explicit check(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

task<> sleep(std::chrono::milliseconds timeout, event_loop& loop = event_loop::current());

inline task<> sleep(int ms, event_loop& loop = event_loop::current()) {
    return sleep(std::chrono::milliseconds{ms}, loop);
}

/// Awaitable returned by yield(): suspends and resumes no earlier than the
/// next event-loop iteration, strictly after every callback, deferred resume
/// and scheduled task that existed when it was enqueued — regardless of
/// which callback phase (timer, idle, poll, check) performed the enqueue.
///
/// This is the primitive for "let the current cascade settle, then decide"
/// patterns (debounced cancellation, coalesced re-checks). Unlike sleep(0) it
/// allocates no timer and does not depend on libuv timer-phase ordering, and
/// unlike the internal deferred-resume queue it never resumes within the
/// current drain cycle.
struct yield_awaiter : io_op {
    explicit yield_awaiter(event_loop& loop) noexcept;

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> h,
                      std::source_location location = std::source_location::current()) noexcept {
        return suspend(h.promise(), location);
    }

    void await_resume() const noexcept {}

private:
    /// Enqueues on the loop's yield queue, then attaches. Defined in loop.cpp.
    std::coroutine_handle<> suspend(task_frame& parent_node, std::source_location loc) noexcept;

    event_loop* loop = nullptr;
};

/// Suspends until the next event-loop iteration.
inline yield_awaiter yield(event_loop& loop = event_loop::current()) {
    return yield_awaiter(loop);
}

}  // namespace kota
