#include <cstddef>
#include <cstdint>
#include <string>
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
    KOTATSU_ANNOTATE(skip = true)
    <int> skipped = 77;
    int second{};
};

struct WithSkipIfField {
    int first{};
    annotation<int, behavior::skip_if<SkipOnDeserialize>> skipped = 88;
    int third{};
};

struct WithFlattenField {
    int first{};
    KOTATSU_ANNOTATE(flatten = true)
    <FlattenInner> inner{};
    int third{};
};

ZEST_SUITE(codec_bincode) {

ZEST_CASE(invalid_optional_tag_returns_error) {
    // An optional tag byte of 2 is invalid (only 0 = none, 1 = some are valid).
    // Attempting to decode an optional<bool> from this should fail.
    const std::vector<std::uint8_t> raw{2U, 1U};
    auto bytes =
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(raw.data()), raw.size());
    std::optional<bool> value;
    auto status = bincode::from_bytes(bytes, value);
    ASSERT(!status);
}

ZEST_CASE(truncated_string_payload_returns_error) {
    auto bytes = bincode::to_bytes(std::string("hello"));
    ASSERT(bytes);

    auto truncated = std::span<const std::byte>(*bytes).first(bytes->size() - 3);
    std::string value;
    auto status = bincode::from_bytes(truncated, value);
    ASSERT(!status);
    EXPECT(status.error().message == "unexpected eof");
}

ZEST_CASE(oversized_length_prefix_returns_error) {
    // A string length prefix of uint64::max with no payload behind it must be
    // rejected as EOF, never used to size a read.
    const std::vector<std::uint8_t> raw(8, 0xFF);
    auto bytes =
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(raw.data()), raw.size());
    std::string value;
    auto status = bincode::from_bytes(bytes, value);
    ASSERT(!status);
    EXPECT(status.error().message == "unexpected eof");
}

ZEST_CASE(struct_deserialize_respects_schema_skip) {
    PlainPair plain{.first = 11, .second = 22};
    auto bytes = bincode::to_bytes(plain);
    ASSERT(bytes);

    WithSkippedField decoded{};
    auto status = bincode::from_bytes(*bytes, decoded);
    ASSERT(status);
    EXPECT(decoded.first == 11);
    EXPECT(annotated_value(decoded.skipped) == 77);
    EXPECT(decoded.second == 22);
}

ZEST_CASE(struct_deserialize_respects_skip_if) {
    PlainTriple plain{.first = 1, .second = 2, .third = 3};
    auto bytes = bincode::to_bytes(plain);
    ASSERT(bytes);

    WithSkipIfField decoded{};
    auto status = bincode::from_bytes(*bytes, decoded);
    ASSERT(status);
    EXPECT(decoded.first == 1);
    EXPECT(annotated_value(decoded.skipped) == 88);
    EXPECT(decoded.third == 3);
}

ZEST_CASE(struct_deserialize_respects_flatten) {
    PlainFlattened plain{.first = 10, .x = 20, .y = 30, .third = 40};
    auto bytes = bincode::to_bytes(plain);
    ASSERT(bytes);

    WithFlattenField decoded{};
    auto status = bincode::from_bytes(*bytes, decoded);
    ASSERT(status);
    EXPECT(decoded.first == 10);
    EXPECT(annotated_value(decoded.inner).x == 20);
    EXPECT(annotated_value(decoded.inner).y == 30);
    EXPECT(decoded.third == 40);
}

};  // ZEST_SUITE(codec_bincode)

}  // namespace

}  // namespace kota::codec
