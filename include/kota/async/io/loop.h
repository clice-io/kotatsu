#pragma once

#include <coroutine>
#include <memory>
#include <source_location>
#include <tuple>
#include <utility>

#include "kota/support/functional.h"
#include "kota/async/runtime/node.h"
#include "kota/async/runtime/task.h"

struct uv_loop_s;
using uv_loop_t = uv_loop_s;

namespace kota {

namespace detail {

/// How the io layer reaches the event loop behind a libuv loop.
struct loop_access;

}  // namespace detail

/// A thread-safe relay for posting callbacks to an event loop.
///
/// Creating a relay keeps the event loop alive until the relay is
/// destroyed.
///
/// A default-constructed or moved-from relay is inert: send() is a
/// safe no-op, and destruction has no effect.
///
/// Usage (one-shot):
///   auto relay = loop.create_relay();
///   some_system_async_api([relay = std::move(relay)](auto result) mutable {
///       relay.send([result] { /* runs on loop thread */ });
///   });
///
/// Usage (recurring):
///   relay notify = loop.create_relay();
///   // from any thread, repeatedly:
///   notify.send([&] { drain_buffer(); });
///   // destroy the relay (or let it go out of scope) to release the loop hold.
///
/// Ownership:
///   The relay object is single-owner and non-copyable. send() is
///   thread-safe with respect to other send() calls, but the relay
///   must not be destroyed while any send() call is in progress.
///
/// Lifetime:
///   The event_loop must outlive all relays created from it. Using a
///   relay after its event_loop is destroyed is undefined behavior.
///
/// Thread safety:
///   - Construction (create_relay) is NOT thread-safe; call it on the
///     loop thread before handing the relay off.
///   - send() is thread-safe and can be called multiple times.
///   - Destroying the relay releases the loop hold. Pending callbacks
///     that were already enqueued are still delivered, unless the
///     event_loop itself is being destroyed (which clears the queue).
class relay {
public:
    relay() noexcept = default;

    relay(const relay&) = delete;
    relay& operator=(const relay&) = delete;

    relay(relay&& other) noexcept;
    relay& operator=(relay&& other) noexcept;

    ~relay();

    /// Posts a callback to the event loop.
    ///
    /// Thread-safe. Can be called multiple times. Callbacks are executed
    /// on the loop thread in FIFO enqueue order. Concurrent producers
    /// are serialized by a mutex, so cross-thread ordering follows
    /// mutex acquisition order.
    void send(function<void()> callback);

private:
    friend class event_loop;

    struct Self;

    explicit relay(Self* p) noexcept;

    Self* self = nullptr;
};

/// Runs an event loop backed by libuv.
///
/// All async operations (tasks, timers, I/O) require an event_loop.
/// Each thread may have at most one active loop (thread-local).
/// Use event_loop::current() inside a running loop to get a reference.
class event_loop {
public:
    event_loop();

    ~event_loop();

    /// Returns the event loop running on the current thread.
    static event_loop& current();

    /// Returns true if a loop is running on the current thread.
    static bool has_current() noexcept;

    /// The libuv loop underneath, for code that runs libuv handles of its
    /// own on this loop. Its `data` belongs to the event_loop.
    uv_loop_t* native_handle() noexcept;

    /// Runs the loop on this thread until it has nothing left to wait for, or
    /// stop() ends it; tasks that wait on nothing but each other or a sync
    /// primitive do not keep it running. Tasks a sync primitive woke while
    /// the loop did not run resume first. Returns 0, or non-zero when stop()
    /// ended it with work left.
    int run();

    /// Makes run() return once the current loop iteration is over.
    void stop();

    /// Creates a relay that keeps this event loop alive until destroyed.
    ///
    /// NOT thread-safe: must be called on the loop thread. The returned relay
    /// object can then be moved to another thread or captured in a system API
    /// callback, where relay::send() can be called thread-safely.
    relay create_relay();

    /// Registers a callback to run during event_loop destruction, before the
    /// underlying uv loop is closed.
    ///
    /// NOT thread-safe: intended for loop-affine subsystems that need to
    /// release handles tied to this loop.
    void on_destroy(function<void()> callback);

    /// Schedules a task to start on this event loop's next turn. Passed as an
    /// rvalue, the task is the loop's, which destroys it once it ends; passed
    /// as an lvalue, it stays with the caller, who can still cancel() it and
    /// read its result() once it ends. Destroying it before it ends lets it
    /// go: it is cancelled, and the loop frees it once it ends. A task
    /// cancelled before it starts never runs.
    template <typename Task>
    void schedule(Task&& task, std::source_location location = std::source_location::current()) {
        schedule(
            detail::task_access::make_root(task, std::is_rvalue_reference_v<Task&&>, location));
    }

private:
    friend class async_node;
    friend class wait_node;
    friend struct detail::loop_access;

    struct Self;

    void schedule(task_frame& root);

    /// Queues the task of a wait a sync primitive granted, to resume once
    /// whatever runs now has suspended instead of inline.
    void defer_resume(wait_node& waiter);

    /// Resumes the queued tasks, in the order they were queued. The runtime
    /// calls this after the outermost coroutine resumption returns; run()
    /// calls it for what was queued while the loop did not run, and a check
    /// handle for what a libuv callback queued outside any resumption.
    void drain_deferred();

    std::unique_ptr<Self> self;
};

/// Awaitable returned by yield(): resumes on a later iteration of the loop,
/// after everything that was due when it suspended, whichever callback it
/// suspended from: the callbacks, the tasks woken and the tasks scheduled.
///
/// This is the primitive for "let the current cascade settle, then decide"
/// patterns (debounced cancellation, coalesced re-checks). Unlike sleep(0) it
/// allocates no timer and does not depend on the order of libuv's phases.
struct yield_awaiter : private io_op {
    explicit yield_awaiter(event_loop& loop) noexcept;

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        return suspend(waiting.promise(), location);
    }

    void await_resume() const noexcept {}

private:
    /// Enqueues on the loop, then attaches.
    std::coroutine_handle<> suspend(task_frame& waiting, std::source_location location) noexcept;

    event_loop* loop = nullptr;
};

/// Suspends until the next event-loop iteration.
inline yield_awaiter yield(event_loop& loop = event_loop::current()) {
    return yield_awaiter(loop);
}

/// Convenience: creates a loop, schedules all tasks, runs it until it has no
/// work left and returns what each task ended with: its value, its error, or
/// that it was cancelled. Rethrows what a task threw. Every task must have
/// ended by then.
template <typename... Tasks>
auto run(Tasks... tasks) {
    event_loop loop;
    (loop.schedule(tasks), ...);
    loop.run();
    return std::tuple(std::move(tasks).catch_cancel().result()...);
}

}  // namespace kota
