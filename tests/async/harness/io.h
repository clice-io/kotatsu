#pragma once

// What the io tests share: finished() and winner() to cancel an operation
// by racing it, and read_to_end() to read a stream to its end.

#include <cstddef>
#include <string>
#include <utility>

#include "kota/async/async.h"

namespace kota::test {

/// A task that finishes at once: when_any cancels what it races as soon as
/// that has started.
inline task<> finished() {
    co_return;
}

/// Which of `first` and `second` ends first, 0 or 1. when_any cancels the
/// other and returns only once it has ended.
template <typename First, typename Second>
task<std::size_t, error> winner(First first, Second second) {
    auto won = co_await when_any(std::move(first), std::move(second));
    if constexpr(is_outcome_v<decltype(won)>) {
        co_return (co_await or_fail(std::move(won))).index();
    } else {
        co_return won.index();
    }
}

/// Everything `reader` reads until the end of the stream.
inline task<std::string, error> read_to_end(stream& reader) {
    std::string all;
    while(true) {
        auto piece = co_await reader.read();
        if(!piece) {
            if(piece.error() != error::end_of_file) {
                co_await fail(piece.error());
            }
            co_return all;
        }
        all += *piece;
    }
}

}  // namespace kota::test
