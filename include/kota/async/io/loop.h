#pragma once

#include <coroutine>
#include <memory>
#include <source_location>
#include <tuple>

#include "kota/support/functional.h"
#include "kota/async/runtime/node.h"

struct uv_loop_s;
using uv_loop_t = uv_loop_s;

namespace kota {

template <typename T = void, typename E = void, typename C = void>
class task;

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

    /// Opaque implementation detail. Defined in loop.cpp.
    struct Self;

private:
    friend class event_loop;

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

    /// Opaque implementation detail. Defined in loop.cpp.
    struct Self;

    /// The libuv loop underneath, for code that runs libuv handles of its
    /// own on this loop. Its `data` belongs to the event_loop.
    uv_loop_t* native_handle() noexcept;

    int run();

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

    /// Schedules a task for execution on this event loop.
    /// If the task is passed by rvalue (temporary), the loop takes ownership
    /// (sets root=true). The task will be destroyed after it completes.
    template <typename Task>
    void schedule(Task&& task, std::source_location location = std::source_location::current()) {
        auto& promise = task.h.promise();
        if constexpr(std::is_rvalue_reference_v<Task&&>) {
            promise.root = true;
            task.release();
        }

        schedule(static_cast<async_node&>(promise), location);
    }

    /// Queues a node for deferred resumption.
    ///
    /// Unlike schedule(), this does not check or modify the node's state.
    /// Used by sync primitives to defer waiter resumes instead of resuming
    /// inline (which would cause reentrancy).
    void defer_resume(async_node& node);

    /// Drains all deferred resumes. The runtime calls this after the outermost
    /// coroutine resume returns; a check handle is kept as a fallback so queued
    /// resumes still run before the next loop iteration.
    void drain_deferred();

private:
    void schedule(async_node& frame, std::source_location location);

    std::unique_ptr<Self> self;
};

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
    /// Enqueues on the loop, then attaches. Defined in loop.cpp.
    std::coroutine_handle<> suspend(async_node& parent_node, std::source_location loc) noexcept;

    event_loop* loop = nullptr;
};

/// Suspends until the next event-loop iteration.
inline yield_awaiter yield(event_loop& loop = event_loop::current()) {
    return yield_awaiter(loop);
}

/// Convenience: creates a loop, schedules all tasks, runs to completion,
/// and returns a tuple of their values (via task::value()).
template <typename... Tasks>
auto run(Tasks&&... tasks) {
    event_loop loop;
    (loop.schedule(tasks), ...);
    loop.run();
    return std::tuple(std::move(tasks.value())...);
}

}  // namespace kota
