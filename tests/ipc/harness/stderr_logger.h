#pragma once

#include <print>
#include <string>
#include <string_view>
#include <utility>

#include "kota/ipc/logger.h"

namespace kota::test {

/// A Peer logger for integration drivers: one `[<level>] <message>` line on
/// stderr per message. The TypeScript harness (driver.ts) fails a test on a
/// warn or error line the test did not expect.
inline ipc::LogCallback stderr_logger() {
    return [](ipc::LogLevel level, std::string message) {
        constexpr std::string_view names[] = {"trace", "debug", "info", "warn", "error"};
        std::println(stderr, "[{}] {}", names[std::to_underlying(level)], message);
    };
}

}  // namespace kota::test
