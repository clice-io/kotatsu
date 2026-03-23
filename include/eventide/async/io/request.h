#pragma once

#include <optional>

#include "eventide/common/function_traits.h"
#include "eventide/common/functional.h"
#include "eventide/async/io/loop.h"
#include "eventide/async/runtime/task.h"
#include "eventide/async/vocab/error.h"

namespace eventide {

/// Run work on libuv's worker pool and complete when finished or with an error.
task<void, error> queue(function<void()> fn, event_loop& loop = event_loop::current());

/// Run work on libuv's worker pool and return either its value or an error.
template <typename Fn, typename R = callable_return_t<std::remove_cvref_t<Fn>>>
    requires std::is_invocable_v<Fn> && (!std::is_void_v<R>)
task<R, error> queue(Fn fn, event_loop& loop = event_loop::current()) {
    std::optional<R> ret;
    co_await queue(function<void()>([&] { ret.emplace(fn()); }), loop).or_fail();
    co_return std::move(*ret);
}

/// Run N work items on libuv's worker pool with a single awaiter.
/// Avoids creating a coroutine frame per task. The callback receives an index [0, count).
task<void, error> queue_batch(std::size_t count,
                              function<void(std::size_t)> fn,
                              event_loop& loop = event_loop::current());

/// Convenience overload: wraps a callable into function<void(std::size_t)>.
template <typename Fn>
    requires std::is_invocable_v<Fn, std::size_t> &&
             (!std::is_same_v<std::remove_cvref_t<Fn>, function<void(std::size_t)>>)
task<void, error> queue_batch(std::size_t count, Fn fn, event_loop& loop = event_loop::current()) {
    return queue_batch(count, function<void(std::size_t)>(std::move(fn)), loop);
}

}  // namespace eventide
