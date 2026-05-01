#pragma once

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

#include "kota/async/async.h"
#include "kota/async/io/fs_event.h"

namespace kota {

// Platform watchers (inotify, FSEvents, ReadDirectoryChangesW) need time to
// register with the kernel after create() returns.  This helper encapsulates
// the stabilisation delay so tests don't scatter magic numbers.
inline task<void, error> wait_for_watcher_ready(event_loop& loop) {
    co_await sleep(500, loop);
}

inline task<std::vector<fs_event::change>, error> next_or_timeout(fs_event& w,
                                                                  event_loop& loop,
                                                                  int timeout_ms = 10000) {
    auto do_timeout = [&]() -> task<std::vector<fs_event::change>, error> {
        co_await sleep(timeout_ms, loop);
        co_await fail(error::connection_timed_out);
    };

    auto result = co_await when_any(w.next(), do_timeout());
    if(result.has_error()) {
        co_await fail(result.error());
    }

    co_return std::visit([](auto&& v) -> std::vector<fs_event::change> { return std::move(v); },
                         std::move(*result));
}

inline bool has_effect(const std::vector<fs_event::change>& changes, fs_event::effect eff) {
    return std::ranges::any_of(changes, [eff](const auto& c) { return c.type == eff; });
}

}  // namespace kota
