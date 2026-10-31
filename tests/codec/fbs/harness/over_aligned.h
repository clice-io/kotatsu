#pragma once

// Over-aligned inline structs for the fbs tests: to_bytes aligns a vector of
// one for the struct, while the verifier itself checks only the 4-byte
// alignment of the vector's length word.

#include <vector>

namespace kotatsu::test {

/// SSE loads of it need 16-byte alignment.
struct alignas(16) Vec4 {
    float x = 0;
    float y = 0;
    float z = 0;
    float w = 0;

    auto operator==(const Vec4&) const -> bool = default;
};

struct Mesh {
    std::vector<Vec4> points;
};

/// Aligned past what operator new guarantees, so no std::vector holding a
/// buffer promises its alignment.
struct alignas(32) Vec8 {
    float a = 0;
    float b = 0;
    float c = 0;
    float d = 0;
    float e = 0;
    float f = 0;
    float g = 0;
    float h = 0;

    auto operator==(const Vec8&) const -> bool = default;
};

struct WideMesh {
    std::vector<Vec8> points;
};

}  // namespace kotatsu::test
