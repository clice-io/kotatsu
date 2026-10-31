#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "kotatsu/zest/zest.h"
#include "kotatsu/codec/blob/blob.h"

namespace kotatsu::test {

enum class Shade : std::uint8_t {
    Red,
    Green,
};

/// A plain record with padding after `flag`.
struct PaddedPoint {
    std::uint32_t x = 0;
    bool flag = false;
    std::uint64_t y = 0;
};

template <template <typename> typename Seq>
struct LeafOf {
    Seq<char> name;
    Seq<std::uint16_t> values;
};

using Leaf = LeafOf<codec::blob::Owned>;

template <template <typename> typename Seq>
struct BadgeOf {
    std::uint32_t id = 0;
    Seq<char> label;
};

using Badge = BadgeOf<codec::blob::Owned>;

/// A struct template that holds no sequence: plain, and packed, but its writer
/// form is another type than its view.
template <template <typename> typename Seq>
struct StampOf {
    std::uint32_t seconds = 0;
    std::uint32_t nanos = 0;
};

/// Every kind of field: scalars, a plain struct template, strings and
/// sequences (empty ones too), a sequence of strings, of plain records, of
/// plain struct templates, of records holding sequences, a nested record and a
/// byte sequence.
template <template <typename> typename Seq>
struct SampleOf {
    bool enabled = false;
    std::uint8_t small = 0;
    std::int32_t negative = 0;
    std::uint64_t wide = 0;
    float ratio = 0;
    double precise = 0;
    Shade color = Shade::Red;
    StampOf<Seq> stamp;
    Seq<char> text;
    Seq<char> empty_text;
    Seq<std::uint32_t> numbers;
    Seq<std::uint64_t> empty_numbers;
    Seq<Seq<char>> names;
    Seq<PaddedPoint> points;
    Seq<StampOf<Seq>> stamps;
    Seq<LeafOf<Seq>> leaves;
    BadgeOf<Seq> badge;
    Seq<std::uint8_t> bytes;
};

using Sample = SampleOf<codec::blob::Owned>;

inline Sample sample() {
    return {
        .enabled = true,
        .small = 7,
        .negative = -5,
        .wide = 1ull << 40,
        .ratio = 0.5f,
        .precise = 2.25,
        .color = Shade::Green,
        .stamp = {.seconds = 11, .nanos = 22},
        .text = "hello",
        .empty_text = {},
        .numbers = {1, 2, 3},
        .empty_numbers = {},
        .names = {"a", "", "ccc"},
        .points = {{1, true, 2}, {3, false, 4}},
        .stamps = {{5, 6}, {7, 8}},
        .leaves = {{"x", {1, 2}}, {"", {}}, {"zz", {9}}},
        .badge = {42, "badge"},
        .bytes = {1, 2, 3, 4, 5},
    };
}

/// A sequence of bools, which std::vector<bool> packs into bits.
template <template <typename> typename Seq>
struct FlagsOf {
    Seq<bool> flags;
};

using Flags = FlagsOf<codec::blob::Owned>;

/// A small root whose bytes the tests spell out: the header, then the bool at
/// 8, the string's Vec at 12 and the sequence's at 20, then "abc" at 28 and
/// the two numbers at 32.
template <template <typename> typename Seq>
struct ProbeOf {
    bool flag = false;
    Seq<char> text;
    Seq<std::uint32_t> numbers;
};

using Probe = ProbeOf<codec::blob::Owned>;

inline Probe probe() {
    return {
        .flag = true,
        .text = "abc",
        .numbers = {1, 2},
    };
}

/// The probe with an empty string and sequence: its header and root alone, 28
/// bytes.
inline Probe empty_probe() {
    return {
        .flag = true,
        .text = {},
        .numbers = {},
    };
}

/// The blob of `value`, which the test fails without.
template <typename T>
std::vector<std::byte> blob_of(const T& value) {
    auto bytes = codec::blob::to_bytes(value);
    ZASSERT(bytes);
    return std::move(*bytes);
}

}  // namespace kotatsu::test
