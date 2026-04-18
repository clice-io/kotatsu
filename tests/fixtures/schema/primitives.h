#pragma once

// Primitive / scalar fixtures for virtual_schema tests.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kota::meta::fixtures {

struct BoolStruct {
    bool v;
};

struct Int8Struct {
    std::int8_t v;
};

struct Int16Struct {
    std::int16_t v;
};

struct Int32Struct {
    std::int32_t v;
};

struct Int64Struct {
    std::int64_t v;
};

struct UInt8Struct {
    std::uint8_t v;
};

struct UInt16Struct {
    std::uint16_t v;
};

struct UInt32Struct {
    std::uint32_t v;
};

struct UInt64Struct {
    std::uint64_t v;
};

struct Float32Struct {
    float v;
};

struct Float64Struct {
    double v;
};

struct CharStruct {
    char v;
};

struct StringStruct {
    std::string v;
};

struct BytesStruct {
    std::vector<std::byte> v;
};

struct AllPrimitives {
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

struct SimpleStruct {
    int x;
    std::string name;
    float score;
};

struct EmptyStruct {};

struct SingleFieldStruct {
    int only;
};

}  // namespace kota::meta::fixtures
