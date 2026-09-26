#pragma once

// Enum fixtures: underlying types and value shapes.

#include <cstdint>
#include <limits>

namespace kota::test {

enum class Color { red, green, blue };

enum class SmallEnum : std::int8_t { a = 1, b = 2 };

enum class SignedEnum : std::int32_t { neg = -42, zero = 0, pos = 42 };

enum class UInt8Enum : std::uint8_t { a = 0, b = 1, c = 255 };

enum class HugeUnsignedEnum : std::uint64_t {
    zero = 0,
    max = std::numeric_limits<std::uint64_t>::max(),
};

/// A char underlying type: the value is still an integer.
enum class Letter : char { a = 'A', z = 'Z' };

/// Enumerator names of several words, so that rename policies show.
enum class Access : std::uint8_t { read_only, full_control };

}  // namespace kota::test
