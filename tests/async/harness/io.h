#pragma once

// What the io tests share: finished() and winner() to cancel an operation
// by racing it.

#include <cstddef>
#include <utility>

#include "kotatsu/async/async.h"

namespace kotatsu::test {

/// A task that finishes at once: when_any cancels what it races as soon as
/// that has started.
inline async::task<> finished() {
    co_return;
}

/// Which of `first` and `second` ends first, 0 or 1. when_any cancels the
/// other and returns only once it has ended.
template <typename First, typename Second>
async::task<std::size_t, async::error> winner(First first, Second second) {
    auto won = co_await async::when_any(std::move(first), std::move(second));
    if constexpr(is_outcome_v<decltype(won)>) {
        auto picked = co_await async::or_fail(std::move(won));
        co_return picked.index();
    } else {
        co_return won.index();
    }
}

}  // namespace kotatsu::test
