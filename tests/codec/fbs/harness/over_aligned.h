#pragma once

// An over-aligned inline struct for the fbs tests: to_bytes aligns a vector
// of it for the struct, while the verifier itself checks only the 4-byte
// alignment of the vector's length word.

#include <vector>

namespace kota::test {

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

}  // namespace kota::test
