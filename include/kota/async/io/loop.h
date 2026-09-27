#pragma once

#include <memory>
#include <source_location>
#include <tuple>
#include <type_traits>
#include <utility>

#include "kota/support/functional.h"

struct uv_loop_s;
using uv_loop_t = uv_loop_s;

namespace kota {

class async_node;
class task_frame;
class wait_node;

template <typename T = void, typename E = void, typename C = void>
class task;

namespace detail {

/// Readies `task` to start as a root, and hands its frame over to the event
/// loop when `owned`. Defined in task.h.
template <typename Task>
task_frame& make_root(Task& task, bool owned, std::source_location location) noexcept;

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

    /// Internal accessor for the implementation struct.
    Self* operator->() {
        return self.get();
    }

public:
    operator uv_loop_t&() noexcept;

    operator const uv_loop_t&() const noexcept;

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

    /// Schedules a task to start on this event loop's next turn. Passed as an
    /// rvalue, the task is the loop's, which destroys it once it ends; passed
    /// as an lvalue, it stays with the caller, who can still cancel() it and
    /// read its result() once it ends. A task cancelled before it starts
    /// never runs.
    template <typename Task>
    void schedule(Task&& task, std::source_location location = std::source_location::current()) {
        schedule(detail::make_root(task, std::is_rvalue_reference_v<Task&&>, location));
    }

private:
    friend class async_node;
    friend class wait_node;

    void schedule(task_frame& root);

    /// Queues a task a sync primitive woke, to resume once whatever runs now
    /// has suspended instead of inline.
    void defer_resume(task_frame& task);

    /// Resumes the queued tasks. The runtime calls this after the outermost
    /// coroutine resumption returns; a check handle is kept as a fallback so
    /// they still run before the next loop iteration.
    void drain_deferred();

    std::unique_ptr<Self> self;
};

/// Convenience: creates a loop, schedules all tasks, runs it until they have
/// ended and returns what each ended with: its value, its error, or that it was
/// cancelled. Rethrows what a task threw.
template <typename... Tasks>
auto run(Tasks... tasks) {
    event_loop loop;
    (loop.schedule(tasks), ...);
    loop.run();
    return std::tuple(std::move(tasks).catch_cancel().result()...);
}

}  // namespace kota
