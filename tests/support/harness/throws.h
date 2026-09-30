#pragma once

#include "kota/support/config.h"

#if KOTA_ENABLE_EXCEPTIONS

namespace kota::test {

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

}  // namespace kota::test

#endif  // KOTA_ENABLE_EXCEPTIONS
