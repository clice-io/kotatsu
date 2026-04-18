#pragma once

// Enum fixtures — one per underlying type plus shape / value edge cases.

#include <cstdint>
#include <limits>

namespace kota::meta::fixtures {

enum class Int8Enum : std::int8_t { a = -1, b = 0, c = 1 };
enum class Int16Enum : std::int16_t { a = -100, b = 0, c = 100 };
enum class Int32Enum : std::int32_t { a = -1, b = 0, c = 1 };
enum class Int64Enum : std::int64_t { a = -1, b = 0, c = 1 };

enum class UInt8Enum : std::uint8_t { a = 0, b = 1, c = 255 };
enum class UInt16Enum : std::uint16_t { a = 0, b = 1, c = 65535 };
enum class UInt32Enum : std::uint32_t { a = 0, b = 1, c = 4294967295U };
enum class UInt64Enum : std::uint64_t {
    zero = 0,
    one = 1,
    max = std::numeric_limits<std::uint64_t>::max(),
};

enum class HugeUnsignedEnum : std::uint64_t {
    zero = 0,
    max = std::numeric_limits<std::uint64_t>::max(),
};

enum class NonContiguousEnum : std::int32_t { a = 1, b = 5, c = 100 };

enum class SignedEnum : std::int32_t { neg = -42, zero = 0, pos = 42 };

enum class SingleMemberEnum : std::int32_t { only = 7 };

enum ColorUnscoped : std::int32_t {
    red_unscoped = 0,
    green_unscoped = 1,
    blue_unscoped = 2,
};

enum class Color { red, green, blue };

enum class SmallEnum : std::int8_t { a = 1, b = 2 };

}  // namespace kota::meta::fixtures
