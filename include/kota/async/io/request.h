#pragma once

#include <optional>
#include <type_traits>

#include "kota/support/functional.h"
#include "kota/async/io/loop.h"
#include "kota/async/runtime/task.h"

namespace kota {

namespace detail {

/// Runs `work` on libuv's thread pool and completes on `loop`; see queue().
task<> run_on_pool(function<void()> work, function<void()> on_cancel, event_loop& loop);

}  // namespace detail

/// Runs `fn` on libuv's thread pool and returns what it returns, resuming on
/// `loop`.
///
/// The pool is libuv's one per process, shared by every loop and by the fs
/// operations. It starts when first used, with as many threads as
/// UV_THREADPOOL_SIZE says then (4 by default, at most 1024), and keeps them:
/// work queued while every thread is busy waits for one.
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

}  // namespace kota
