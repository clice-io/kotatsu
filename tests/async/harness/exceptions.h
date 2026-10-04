#pragma once

// What the tests that read a thrown exception share: thrown(), and
// exceptions_unreadable for the build that cannot read one.

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "kota/support/config.h"

namespace kota::test {

// clang-cl's ASan hands an exception handler a broken reference to the
// exception, so reading what was thrown crashes there. Cases that read it are
// declared `skip = test::exceptions_unreadable`.
#if defined(_WIN32) && defined(__clang__)
#if __has_feature(address_sanitizer)
#define KOTA_TEST_EXCEPTIONS_UNREADABLE
#endif
#endif

#ifdef KOTA_TEST_EXCEPTIONS_UNREADABLE
constexpr bool exceptions_unreadable = true;
#else
constexpr bool exceptions_unreadable = false;
#endif

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
