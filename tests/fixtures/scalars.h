#pragma once

// Scalar fixtures: every scalar kind, text and bytes.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

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

    static Scalars typical() {
        return {
            .b = true,
            .i8 = -42,
            .i16 = -1000,
            .i32 = -100000,
            .i64 = -9999999999LL,
            .u8 = 200,
            .u16 = 50000,
            .u32 = 3000000000U,
            .u64 = 10000000000ULL,
            .f32 = 3.14F,
            .f64 = 2.718281828,
            .c = 'Z',
            .s = "hello scalars",
        };
    }

    /// Each kind's lowest value. The character is the lowest printable ASCII
    /// one: the text backends differ beyond ASCII, which their own tests pin.
    static Scalars lowest() {
        return {
            .b = false,
            .i8 = std::numeric_limits<std::int8_t>::lowest(),
            .i16 = std::numeric_limits<std::int16_t>::lowest(),
            .i32 = std::numeric_limits<std::int32_t>::lowest(),
            .i64 = std::numeric_limits<std::int64_t>::lowest(),
            .u8 = 0,
            .u16 = 0,
            .u32 = 0,
            .u64 = 0,
            .f32 = std::numeric_limits<float>::lowest(),
            .f64 = std::numeric_limits<double>::lowest(),
            .c = ' ',
            .s = "",
        };
    }

    /// Each kind's highest value, except that uint64 stops at int64's maximum,
    /// the highest every backend carries; the root uint64 case takes the rest
    /// of the range. The character is the highest printable ASCII one.
    static Scalars highest() {
        return {
            .b = true,
            .i8 = std::numeric_limits<std::int8_t>::max(),
            .i16 = std::numeric_limits<std::int16_t>::max(),
            .i32 = std::numeric_limits<std::int32_t>::max(),
            .i64 = std::numeric_limits<std::int64_t>::max(),
            .u8 = std::numeric_limits<std::uint8_t>::max(),
            .u16 = std::numeric_limits<std::uint16_t>::max(),
            .u32 = std::numeric_limits<std::uint32_t>::max(),
            .u64 = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
            .f32 = std::numeric_limits<float>::max(),
            .f64 = std::numeric_limits<double>::max(),
            .c = '~',
            .s = std::string(512, 'x'),
        };
    }
};

/// Text the writers must escape or pass through untouched.
struct Strings {
    std::string empty;
    std::string unicode;
    std::string escapes;
    std::string controls;

    static Strings typical() {
        return {
            .empty = "",
            .unicode = "héllo, 世界",
            .escapes = R"(quote " backslash \ slash /)",
            .controls = "newline \n tab \t return \r bell \x07 unit \x1f",
        };
    }
};

struct Bytes {
    std::vector<std::byte> empty;
    std::vector<std::byte> octets;

    static Bytes typical() {
        return {
            .empty = {},
            .octets = {std::byte{0}, std::byte{1}, std::byte{127}, std::byte{255}},
        };
    }
};

}  // namespace kota::test
