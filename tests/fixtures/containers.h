#pragma once

// Container fixtures: a range that is no container, a container that only
// value-initializes, and pointee comparison for the fixtures that hold
// pointers.

#include <cstddef>
#include <iterator>
#include <ranges>
#include <vector>

#include "kota/support/ranges.h"
#include "kota/meta/compare.h"

namespace kota::test {

/// A list whose default constructor is explicit, as llvm::DenseMap's is.
struct ExplicitList : std::vector<int> {
    using std::vector<int>::vector;

    explicit ExplicitList() = default;
};

/// An aggregate that only value-initializes: `HoldsExplicit{}`
/// copy-list-initializes `list` from `{}`, which its explicit default
/// constructor rejects.
struct HoldsExplicit {
    ExplicitList list;
    int count;

    auto operator==(const HoldsExplicit&) const -> bool = default;
};

/// Smart pointers equal by what they point to. meta::eq compares pointers by
/// address, so fixtures holding them define operator== through this.
template <typename Pointer>
bool same_pointee(const Pointer& lhs, const Pointer& rhs) {
    if(!lhs || !rhs) {
        return !lhs && !rhs;
    }
    return meta::eq(*lhs, *rhs);
}

// An input_range whose reference type is itself, auto-detected as
// range_format::disabled so it never looks like array / set / map.
struct DisabledRange;

struct DisabledRangeIter {
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = DisabledRange;
    using difference_type = std::ptrdiff_t;

    auto operator*() const -> DisabledRange;

    auto operator++() -> DisabledRangeIter& {
        return *this;
    }

    auto operator++(int) -> DisabledRangeIter {
        return *this;
    }

    auto operator==(std::default_sentinel_t) const -> bool {
        return true;
    }
};

struct DisabledRange {
    auto begin() const -> DisabledRangeIter {
        return {};
    }

    auto end() const -> std::default_sentinel_t {
        return {};
    }
};

inline auto DisabledRangeIter::operator*() const -> DisabledRange {
    return {};
}

static_assert(std::ranges::input_range<DisabledRange>);
static_assert(kota::format_kind<DisabledRange> == kota::range_format::disabled);

}  // namespace kota::test
