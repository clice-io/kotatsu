#pragma once

#include <memory>
#include <source_location>
#include <tuple>

#include "eventide/common/functional.h"

struct uv_loop_s;
using uv_loop_t = uv_loop_s;

namespace eventide {

class async_node;

template <typename T = void, typename E = void, typename C = void>
class task;

/// A one-shot relay for posting a callback to an event loop from an
/// external context (e.g. a system async API callback).
///
/// Unlike event_loop::post(), creating a relay keeps the event loop alive
/// until the relay is used or destroyed. This is useful when you call a
/// system async API and need the loop to stay running until the API's
/// callback fires.
///
/// Usage:
///   auto relay = loop.create_relay();   // keeps loop alive
///   some_system_async_api([relay = std::move(relay)](auto result) mutable {
///       relay.send([result] { /* handle result on loop thread */ });
///   });
///
/// Thread safety:
///   - Construction (create_relay) is NOT thread-safe; call it on the
///     loop thread before handing the relay off.
///   - send() IS thread-safe; it can be called from any thread.
///   - send() may be called at most once. After send(), the relay
///     releases its hold on the loop.
///   - If the relay is destroyed without calling send(), it also
///     releases its hold on the loop.
class relay {
public:
    relay(const relay&) = delete;
    relay& operator=(const relay&) = delete;

    relay(relay&& other) noexcept;
    relay& operator=(relay&& other) noexcept;

    ~relay();

    /// Posts a callback to the event loop and releases the loop hold.
    ///
    /// Thread-safe: can be called from any thread. The callback will be
    /// invoked on the event loop thread during a subsequent iteration.
    /// May be called at most once; subsequent calls are no-ops.
    void send(function<void()> callback);

    /// Opaque implementation detail. Defined in loop.cpp.
    struct self;

private:
    friend class event_loop;

    explicit relay(self* p) noexcept;

    self* self;
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

    /// Opaque implementation detail. Defined in loop.cpp.
    struct self;

    /// Internal accessor for the implementation struct.
    self* operator->() {
        return self.get();
    }

    friend class async_node;

public:
    operator uv_loop_t&() noexcept;

    operator const uv_loop_t&() const noexcept;

    int run();

    void stop();

    /// Posts a callback to be executed on this event loop's thread.
    ///
    /// Thread-safe: can be called from any thread. The callback will be
    /// invoked on the event loop thread during a subsequent iteration.
    /// Internally uses uv_async_t to wake up the loop.
    void post(function<void()> callback);

    /// Creates a relay that keeps this event loop alive until used or destroyed.
    ///
    /// NOT thread-safe: must be called on the loop thread. The returned relay
    /// object can then be moved to another thread or captured in a system API
    /// callback, where relay::send() can be called thread-safely.
    relay create_relay();

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

private:
    void schedule(async_node& frame, std::source_location location);

    std::unique_ptr<self> self;
};

/// Convenience: creates a loop, schedules all tasks, runs to completion,
/// and returns a tuple of their values (via task::value()).
template <typename... Tasks>
auto run(Tasks&&... tasks) {
    event_loop loop;
    (loop.schedule(tasks), ...);
    loop.run();
    return std::tuple(std::move(tasks.value())...);
}

}  // namespace eventide
