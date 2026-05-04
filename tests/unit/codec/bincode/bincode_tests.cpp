#include <cstddef>
#include <cstdint>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/codec/bincode/bincode.h"

namespace kota::codec {

using namespace meta;

namespace {

struct SkipOnDeserialize {
    constexpr bool operator()(const int& /*value*/, bool is_serialize) const noexcept {
        return !is_serialize;
    }
};

struct PlainPair {
    int first{};
    int second{};
};

struct PlainTriple {
    int first{};
    int second{};
    int third{};
};

struct FlattenInner {
    int x{};
    int y{};
};

struct PlainFlattened {
    int first{};
    int x{};
    int y{};
    int third{};
};

struct WithSkippedField {
    int first{};
    annotation<int, attrs::skip> skipped = 77;
    int second{};
};

struct WithSkipIfField {
    int first{};
    annotation<int, behavior::skip_if<SkipOnDeserialize>> skipped = 88;
    int third{};
};

struct WithFlattenField {
    int first{};
    annotation<FlattenInner, attrs::flatten> inner{};
    int third{};
};

TEST_SUITE(serde_bincode) {

TEST_CASE(invalid_optional_tag_produces_error) {
    // Byte 0x02 is an invalid optional tag (valid: 0x00=None, 0x01=Some).
    // The trailing 0x01 would be a valid bool, but should never be reached.
    const std::vector<std::uint8_t> bytes{2U, 1U};
    auto result = bincode::from_bytes<std::optional<bool>>(bytes);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), bincode::error_kind::type_mismatch);
}

TEST_CASE(struct_deserialize_respects_schema_skip) {
    PlainPair plain{.first = 11, .second = 22};
    auto bytes = bincode::to_bytes(plain);
    ASSERT_TRUE(bytes.has_value());

    WithSkippedField decoded{};
    auto status = bincode::from_bytes(*bytes, decoded);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(decoded.first, 11);
    EXPECT_EQ(annotated_value(decoded.skipped), 77);
    EXPECT_EQ(decoded.second, 22);
}

TEST_CASE(struct_deserialize_respects_skip_if) {
    PlainTriple plain{.first = 1, .second = 2, .third = 3};
    auto bytes = bincode::to_bytes(plain);
    ASSERT_TRUE(bytes.has_value());

    WithSkipIfField decoded{};
    auto status = bincode::from_bytes(*bytes, decoded);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(decoded.first, 1);
    EXPECT_EQ(annotated_value(decoded.skipped), 88);
    EXPECT_EQ(decoded.third, 3);
}

TEST_CASE(struct_deserialize_respects_flatten) {
    PlainFlattened plain{.first = 10, .x = 20, .y = 30, .third = 40};
    auto bytes = bincode::to_bytes(plain);
    ASSERT_TRUE(bytes.has_value());

    WithFlattenField decoded{};
    auto status = bincode::from_bytes(*bytes, decoded);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(decoded.first, 10);
    EXPECT_EQ(annotated_value(decoded.inner).x, 20);
    EXPECT_EQ(annotated_value(decoded.inner).y, 30);
    EXPECT_EQ(decoded.third, 40);
}

};  // TEST_SUITE(serde_bincode)

}  // namespace

}  // namespace kota::codec
