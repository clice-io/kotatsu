#pragma once

// Shared helpers for the async runtime tests (runtime/when/, runtime/task_group/).
// Provides the coroutine factories (ready_int/delayed_int/...) and the
// deferred_cancel_await awaiter used to hold a child "cancelling" until a driver
// finishes it. Anything used by only one file lives in that file instead.
// Type-level result-type checks for when_all/when_any live in when/all_values.cpp.

#include <cassert>
#include <coroutine>
#include <source_location>

#include "kota/async/async.h"

namespace kota {

// An awaiter that never completes on its own: it registers as an io_op and only
// finishes when finish_pending_cancel() is called. Used to keep a cancelled
// child pending so tests can observe structured completion (the aggregate waits
// for the child's async cancel to finish) and frame destruction ordering.
struct deferred_cancel_await : io_op {
    static deferred_cancel_await*& pending() {
        thread_local deferred_cancel_await* p = nullptr;
        return p;
    }

    int* destroyed = nullptr;

    explicit deferred_cancel_await(int& destroyed_count) : destroyed(&destroyed_count) {
        assert(pending() == nullptr && "only one deferred_cancel_await may be pending at a time");
        action = &on_cancel;
        pending() = this;
    }

    deferred_cancel_await(const deferred_cancel_await&) = delete;
    deferred_cancel_await& operator=(const deferred_cancel_await&) = delete;
    deferred_cancel_await(deferred_cancel_await&&) = delete;
    deferred_cancel_await& operator=(deferred_cancel_await&&) = delete;

    ~deferred_cancel_await() {
        if(destroyed) {
            *destroyed += 1;
        }
        if(pending() == this) {
            pending() = nullptr;
        }
    }

    static void on_cancel(io_op*) {}

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        return this->attach(waiting.promise(), location);
    }

    void await_resume() const noexcept {}

    static void finish_pending_cancel() {
        auto* op = pending();
        assert(op != nullptr && "finish_pending_cancel requires a pending awaiter");
        pending() = nullptr;
        op->complete();
    }
};

struct custom_error {
    int code = 0;

    friend bool operator==(const custom_error&, const custom_error&) = default;
};

template <typename Group, typename TaskType>
concept group_spawnable = requires(Group group, TaskType task) { group.spawn(std::move(task)); };

inline task<int> ready_int(int value) {
    co_return value;
}

inline task<> ready_void() {
    co_return;
}

inline task<int> delayed_int(int ms, int value) {
    co_await sleep(ms);
    co_return value;
}

inline task<int, error> return_value(int val) {
    co_return val;
}

inline task<int, error> delayed_return_error(int ms, error err) {
    co_await sleep(ms);
    co_await fail(err);
    std::unreachable();
}

inline task<int, error> delayed_return_value(int ms, int val) {
    co_await sleep(ms);
    co_return val;
}

}  // namespace kota
