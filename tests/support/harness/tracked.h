#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

#include "kota/support/config.h"

namespace kota::test {

/// Counts what happens to the Tracked values of its thread while it is alive; the innermost
/// census counts. A copy or move can be armed to throw, to see what an operation leaves
/// behind when an element's constructor fails.
struct Census {
    int live = 0;
    int copies = 0;
    int moves = 0;
    int assignments = 0;
    /// How many more copies or throwing moves succeed before one throws; -1 for all.
    int throw_after = -1;

    Census() : outer(innermost) {
        innermost = this;
    }

    Census(const Census&) = delete;
    Census& operator=(const Census&) = delete;

    ~Census() {
        innermost = outer;
    }

    static Census& current() {
        static thread_local Census fallback(nullptr);
        return innermost != nullptr ? *innermost : fallback;
    }

    /// Called by a construction that the armed count lets throw.
    void construct_or_throw() {
        if(throw_after == 0) {
            throw_after = -1;
            KOTA_THROW(std::runtime_error("tracked construction failed"));
        }
        if(throw_after > 0) {
            throw_after -= 1;
        }
    }

private:
    explicit Census(std::nullptr_t) : outer(nullptr) {}

    inline static thread_local Census* innermost = nullptr;
    Census* outer;
};

/// An element that reports its life to the current census. It keeps its value in a heap cell,
/// so that one leaked or destroyed twice is also an ASan report; a moved-from one has none.
/// `NothrowMove` says whether moves are noexcept, or count towards Census::throw_after as
/// copies always do.
template <bool NothrowMove>
struct BasicTracked {
    std::unique_ptr<int> cell;

    BasicTracked() : BasicTracked(0) {}

    /// Implicit, so that `{1, 2, 3}` lists values.
    BasicTracked(int value) : cell(std::make_unique<int>(value)) {
        Census::current().live += 1;
    }

    BasicTracked(const BasicTracked& other) {
        auto& census = Census::current();
        census.construct_or_throw();
        cell = other.cell ? std::make_unique<int>(*other.cell) : nullptr;
        census.copies += 1;
        census.live += 1;
    }

    BasicTracked(BasicTracked&& other) noexcept(NothrowMove) {
        auto& census = Census::current();
        if constexpr(!NothrowMove) {
            census.construct_or_throw();
        }
        cell = std::move(other.cell);
        census.moves += 1;
        census.live += 1;
    }

    BasicTracked& operator=(const BasicTracked& other) {
        if(this != &other) {
            cell = other.cell ? std::make_unique<int>(*other.cell) : nullptr;
        }
        Census::current().assignments += 1;
        return *this;
    }

    BasicTracked& operator=(BasicTracked&& other) noexcept {
        cell = std::move(other.cell);
        Census::current().assignments += 1;
        return *this;
    }

    ~BasicTracked() {
        Census::current().live -= 1;
    }

    /// The value, or -1 when moved from.
    int value() const {
        return cell ? *cell : -1;
    }
};

using Tracked = BasicTracked<true>;

/// A Tracked whose moves can throw.
using ThrowingTracked = BasicTracked<false>;

/// The values of `elements`, which checks compare and print as plain integers.
template <typename Range>
std::vector<int> values(const Range& elements) {
    std::vector<int> result;
    for(const auto& element: elements) {
        result.push_back(element.value());
    }
    return result;
}

}  // namespace kota::test
