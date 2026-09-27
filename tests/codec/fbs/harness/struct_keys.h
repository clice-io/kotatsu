#pragma once

// A struct map key for the fbs tests: it inlines, so the encoder stores it as
// its image, sorts entries field by field, and the map views binary-search
// it.

#include <cstdint>
#include <map>
#include <tuple>

namespace kota::test {

/// Sentinel defaults break std::is_trivial but not trivial copyability.
struct SentinelRange {
    std::uint32_t begin = static_cast<std::uint32_t>(-1);
    std::uint32_t end = static_cast<std::uint32_t>(-1);

    friend bool operator==(const SentinelRange&, const SentinelRange&) = default;
};

/// The signed weight makes the order field by field differ from the order of
/// the bytes: -2 sorts before 3 field by field, after it byte by byte.
struct OccurrenceKey {
    SentinelRange range;
    std::uint64_t target = 0;
    std::int32_t weight = 0;

    friend bool operator==(const OccurrenceKey&, const OccurrenceKey&) = default;
};

/// The reflected field order.
struct OccurrenceKeyLess {
    bool operator()(const OccurrenceKey& a, const OccurrenceKey& b) const {
        return std::tie(a.range.begin, a.range.end, a.target, a.weight) <
               std::tie(b.range.begin, b.range.end, b.target, b.weight);
    }
};

struct StructKeyed {
    std::map<OccurrenceKey, std::int32_t, OccurrenceKeyLess> hits;

    friend bool operator==(const StructKeyed&, const StructKeyed&) = default;
};

}  // namespace kota::test
