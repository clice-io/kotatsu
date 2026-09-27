#pragma once

#include <coroutine>
#include <source_location>

#include "kota/async/runtime/node.h"

namespace kota::test {

/// An operation that completes only when the test calls complete(). Cancelling
/// it just marks it cancelled, as I/O whose cancellation the operating system
/// still has to confirm: tests use it to see what waits for a cancellation to
/// complete. Await it by reference; it must outlive the await.
struct PendingOp : io_op {
    PendingOp() {
        action = [](io_op*) {
        };
    }

    PendingOp(const PendingOp&) = delete;
    PendingOp& operator=(const PendingOp&) = delete;

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        return attach(waiting.promise(), location);
    }

    void await_resume() const noexcept {}
};

}  // namespace kota::test
