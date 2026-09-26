#pragma once

// Scalar fixtures.

#include <cstdint>
#include <string>

namespace kota::test {

/// One field of every scalar kind.
struct Scalars {
    bool b;
    std::int8_t i8;
    std::int16_t i16;
    std::int32_t i32;
    std::int64_t i64;
    std::uint8_t u8;
    std::uint16_t u16;
    std::uint32_t u32;
    std::uint64_t u64;
    float f32;
    double f64;
    char c;
    std::string s;
};

}  // namespace kota::test
