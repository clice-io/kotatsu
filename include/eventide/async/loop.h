#pragma once

#include <memory>
#include <source_location>
#include <tuple>

struct uv_loop_s;
using uv_loop_t = uv_loop_s;

namespace eventide {

class async_node;

template <typename T = void, typename E = void, typename C = void>
class task;

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
