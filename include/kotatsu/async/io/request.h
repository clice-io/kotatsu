#pragma once

#include <optional>
#include <type_traits>

#include "kotatsu/support/functional.h"
#include "kotatsu/async/io/loop.h"
#include "kotatsu/async/runtime/task.h"
#include "kotatsu/async/vocab/error.h"

namespace kotatsu::async {

namespace detail {

/// Runs `work` on libuv's thread pool, resuming on `loop`; see queue().
task<> run_on_pool(function<void()> work, function<void()> on_cancel, event_loop& loop);

}  // namespace detail

/// Sizes libuv's thread pool, on which queue() and async::fs run, at
/// `threads` and starts it at once; fails with error::invalid_argument for a
/// size not from 1 to 1024. The pool is one for the whole process: one that
/// any use in the process started before stays as it is, which the call
/// cannot tell, and the call waits for one of its threads to be free. Call
/// it at startup, before other threads, and before the first queue(), fs
/// operation or DNS lookup. libuv reads the size from the UV_THREADPOOL_SIZE
/// environment variable, which the call sets while the pool starts and then
/// puts back as it was: children do not inherit it.
error set_thread_pool_size(unsigned threads);

/// Runs `fn` on libuv's thread pool and returns what it returns. The pool is
/// one for the whole process, shared by every loop and by the fs operations,
/// and sized by set_thread_pool_size(), or else at its first use from the
/// UV_THREADPOOL_SIZE environment variable (4 threads by default). Once
/// sys::on_crash is set up, `fn` runs with its thread's crash stack.
///
/// If the awaiting task is cancelled while `fn` is still queued, `fn` is
/// dequeued and never runs, and `on_cancel` is not called. Once `fn` runs it
/// cannot be interrupted: `on_cancel` is how it learns it should return
/// early, and the task ends cancelled once `fn` has returned.
///
/// `on_cancel` runs on the loop thread, at most once, and can still run
/// after `fn` has already finished. Keep it cheap, and only touch state that
/// is safe to share with the concurrently running `fn`: the typical shape is
/// setting an atomic flag that `fn` polls.
template <typename Fn, typename R = std::invoke_result_t<Fn&>>
task<R> queue(Fn fn, function<void()> on_cancel = [] {}, event_loop& loop = event_loop::current()) {
    if constexpr(std::is_void_v<R>) {
        co_await detail::run_on_pool(std::move(fn), std::move(on_cancel), loop);
    } else {
        std::optional<R> value;
        co_await detail::run_on_pool([&] { value.emplace(fn()); }, std::move(on_cancel), loop);
        co_return std::move(*value);
    }
}

/// Runs `fn` on libuv's thread pool, resuming on `loop`, with no cancellation
/// hook.
template <typename Fn>
task<std::invoke_result_t<Fn&>> queue(Fn fn, event_loop& loop) {
    return queue(std::move(fn), [] {}, loop);
}

}  // namespace kotatsu::async
