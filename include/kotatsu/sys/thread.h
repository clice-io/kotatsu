#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <system_error>
#include <type_traits>
#include <utility>

// A thread of the size of stack the caller picks, which std::thread cannot be
// given, started without throwing.

namespace kotatsu::sys {

namespace detail {

/// What a new Thread runs: `run` with it, which deletes it.
struct ThreadEntry {
    void (*run)(ThreadEntry* entry);
};

}  // namespace detail

/// A thread, joined when this goes unless it was joined or detached before,
/// as std::jthread's is. A default-constructed or moved-from Thread is none.
class Thread {
public:
    Thread() noexcept = default;

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    Thread(Thread&& other) noexcept : handle(std::exchange(other.handle, 0)) {}

    /// Joins the thread this holds first.
    Thread& operator=(Thread&& other) noexcept;

    ~Thread();

    /// Starts a thread that calls `fn`, a copy of it, with a stack of
    /// `stack_size` bytes, rounded up to a whole page and to the least the
    /// system takes; on Windows the stack's reserve, which it commits as it
    /// grows; glibc's holds the thread's static TLS too. An exception that
    /// leaves `fn` ends the process, as from a std::thread's. Once on_crash
    /// is set up, the thread gets its crash stack before `fn` runs. The
    /// system's error when it starts none: resource_unavailable_try_again or
    /// not_enough_memory, or invalid_argument for a size it refuses.
    template <typename F>
    static std::expected<Thread, std::error_code> start(std::size_t stack_size, F&& fn) {
        using Fn = std::decay_t<F>;
        auto body = std::make_unique<Body<Fn>>(std::forward<F>(fn));
        auto thread = launch(stack_size, body.get());
        if(thread) {
            // The thread deletes it.
            body.release();
        }
        return thread;
    }

    /// Waits for the thread to end. It must be one, not the calling thread,
    /// and neither joined nor detached yet; it is none after.
    void join();

    /// Lets the thread run on without this, which is none after. It must be
    /// one.
    void detach();

private:
    template <typename Fn>
    struct Body : detail::ThreadEntry {
        template <typename F>
        explicit Body(F&& fn) : ThreadEntry{call}, fn(std::forward<F>(fn)) {}

        /// noexcept: an exception that leaves `fn` ends the process here,
        /// on every host.
        static void call(ThreadEntry* entry) noexcept {
            std::unique_ptr<Body> body(static_cast<Body*>(entry));
            body->fn();
        }

        Fn fn;
    };

    static std::expected<Thread, std::error_code> launch(std::size_t stack_size,
                                                         detail::ThreadEntry* entry);

    /// The pthread_t, or on Windows the HANDLE; 0 for none, which neither
    /// is.
    std::uintptr_t handle = 0;
};

}  // namespace kotatsu::sys
