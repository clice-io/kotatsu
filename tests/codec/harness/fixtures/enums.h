#pragma once

// Enum fixtures only the codec's tests use.

#include <cstdint>

namespace kota::test {

enum class SignedEnum : std::int32_t { neg = -42, zero = 0, pos = 42 };

enum class UInt8Enum : std::uint8_t { a = 0, b = 1, c = 255 };

/// A char underlying type: the value is still an integer.
enum class Letter : char { a = 'A', z = 'Z' };

/// Enumerator names of several words, so that rename policies show.
enum class Access : std::uint8_t { read_only, full_control };

}  // namespace kota::test
