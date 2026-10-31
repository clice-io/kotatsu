#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "kotatsu/support/config.h"

#if KOTATSU_ENABLE_EXCEPTIONS

namespace kotatsu::test {

/// Whether calling `action` throws an Exception, as opposed to nothing or something else.
template <typename Exception, typename Action>
bool throws(Action&& action) {
    try {
        action();
    } catch(const Exception&) {
        return true;
    } catch(...) {
        return false;
    }
    return false;
}

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

}  // namespace kotatsu::test

#endif  // KOTATSU_ENABLE_EXCEPTIONS
