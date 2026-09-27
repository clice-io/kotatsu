#pragma once

// Enum fixtures: underlying types and value shapes.

#include <cstdint>
#include <limits>

namespace kota::test {

enum class Color { red, green, blue };

enum class SmallEnum : std::int8_t { a = 1, b = 2 };

enum class HugeUnsignedEnum : std::uint64_t {
    zero = 0,
    max = std::numeric_limits<std::uint64_t>::max(),
};

}  // namespace kota::test
