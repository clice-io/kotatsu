#include "../standard_case_suite.h"
#include "kota/zest/zest.h"
#include "kota/codec/flatcode/flatcode.h"

namespace kota::codec {

namespace {

using flatcode::from_flatcode;
using flatcode::object_error_code;
using flatcode::to_flatcode;

auto rt = []<typename T>(const T& input) -> std::expected<T, object_error_code> {
    auto encoded = to_flatcode(input);
    if(!encoded) {
        return std::unexpected(encoded.error());
    }
    if(encoded->empty()) {
        return std::unexpected(object_error_code::invalid_state);
    }
    return from_flatcode<T>(*encoded);
};

TEST_SUITE(serde_flatcode_standard) {

SERDE_STANDARD_TEST_CASES_PRIMITIVES(rt)
SERDE_STANDARD_TEST_CASES_NUMERIC_BOUNDARIES(rt)
SERDE_STANDARD_TEST_CASES_TUPLE_LIKE(rt)
SERDE_STANDARD_TEST_CASES_SEQUENCE_SET(rt)
SERDE_STANDARD_TEST_CASES_MAPS(rt)
SERDE_STANDARD_TEST_CASES_OPTIONAL(rt)
SERDE_STANDARD_TEST_CASES_COMPLEX(rt)

};  // TEST_SUITE(serde_flatcode_standard)

TEST_SUITE(serde_flatcode_buffer) {

TEST_CASE(header_has_magic_evfc) {
    struct tiny {
        std::int32_t x{};
        auto operator==(const tiny&) const -> bool = default;
    };

    auto encoded = to_flatcode(tiny{.x = 42});
    ASSERT_TRUE(encoded.has_value());
    ASSERT_GE(encoded->size(), 8U);

    // First 4 bytes = 'EVFC' little-endian (0x43465645).
    EXPECT_EQ((*encoded)[0], static_cast<std::uint8_t>('E'));
    EXPECT_EQ((*encoded)[1], static_cast<std::uint8_t>('V'));
    EXPECT_EQ((*encoded)[2], static_cast<std::uint8_t>('F'));
    EXPECT_EQ((*encoded)[3], static_cast<std::uint8_t>('C'));
}

TEST_CASE(corrupt_magic_yields_invalid_state) {
    struct tiny {
        std::int32_t x{};
        auto operator==(const tiny&) const -> bool = default;
    };

    auto encoded = to_flatcode(tiny{.x = 1});
    ASSERT_TRUE(encoded.has_value());
    (*encoded)[0] ^= 0xFFU;

    auto decoded = from_flatcode<tiny>(*encoded);
    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error(), object_error_code::invalid_state);
}

};  // TEST_SUITE(serde_flatcode_buffer)

}  // namespace

}  // namespace kota::codec
