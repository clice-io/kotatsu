#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace eventide::ipc {

enum class LogLevel : std::uint8_t {
    trace = 0,
    debug = 1,
    info = 2,
    warn = 3,
    error = 4,
};

using LogCallback = std::function<void(LogLevel, std::string)>;

}  // namespace eventide::ipc
