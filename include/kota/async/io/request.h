#pragma once

#include <optional>

#include "kota/support/function_traits.h"
#include "kota/support/functional.h"
#include "kota/async/io/loop.h"
#include "kota/async/runtime/task.h"
#include "kota/async/vocab/error.h"

namespace kota {

/// Run work on libuv's worker pool and complete when finished or with an error.
task<void, error> queue(function<void()> fn, event_loop& loop = event_loop::current());

/// Run work on libuv's worker pool and return either its value or an error.
template <typename Fn, typename R = callable_return_t<Fn>>
    requires std::is_invocable_v<Fn> && (!std::is_void_v<R>)
task<R, error> queue(Fn fn, event_loop& loop = event_loop::current()) {
    std::optional<R> ret;
    co_await queue(function<void()>([&] { ret.emplace(fn()); }), loop).or_fail();
    co_return std::move(*ret);
}

}  // namespace kota
