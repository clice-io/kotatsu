#pragma once

// What the tests that read a thrown exception share.

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "kota/support/config.h"

namespace kota::test {

#if KOTA_ENABLE_EXCEPTIONS
/// The message of the `Exception` that `fn` throws, or nothing when it
/// throws none; an exception of another type propagates and fails the test.
template <typename Exception = std::runtime_error, typename Fn>
std::optional<std::string> thrown(Fn&& fn) {
    try {
        std::forward<Fn>(fn)();
    } catch(const Exception& e) {
        return e.what();
    }
    return std::nullopt;
}
#endif

}  // namespace kota::test
