#pragma once

#include <string>
#include <variant>
#include <vector>

#include "kota/async/io/fs_event.h"

namespace kota {

inline task<std::vector<fs_event::change>, error> next_or_timeout(fs_event& w,
                                                                  event_loop& loop,
                                                                  int timeout_ms = 10000) {
    auto do_timeout = [&]() -> task<std::vector<fs_event::change>, error> {
        co_await sleep(timeout_ms, loop);
        co_return std::vector<fs_event::change>{};
    };

    auto result = co_await when_any(w.next(), do_timeout());
    if(result.has_error()) {
        co_await fail(result.error());
    }

    co_return std::visit([](auto&& v) -> std::vector<fs_event::change> { return std::move(v); },
                         std::move(*result));
}

inline bool has_effect(const std::vector<fs_event::change>& changes, fs_event::effect eff) {
    for(const auto& c: changes) {
        if(c.type == eff)
            return true;
    }
    return false;
}

}  // namespace kota
