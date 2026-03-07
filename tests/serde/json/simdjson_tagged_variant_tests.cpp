#include <string>
#include <variant>

#include "eventide/zest/zest.h"
#include "eventide/serde/json/simd_deserializer.h"
#include "eventide/serde/json/simd_serializer.h"
#include "eventide/serde/serde.h"

namespace eventide::serde {

namespace {

using json::simd::from_json;
using json::simd::to_json;

struct Basic {
    bool is_valid{};
    std::int32_t i32{};

    auto operator==(const Basic&) const -> bool = default;
};

// ── Externally tagged ──────────────────────────────────────────────

using ExtVariant = annotation<std::variant<int, std::string, Basic>,
                              schema::externally_tagged<"integer", "text", "basic">>;

struct ExtTaggedHolder {
    std::string name;
    ExtVariant data;

    auto operator==(const ExtTaggedHolder&) const -> bool = default;
};

// ── Adjacently tagged ──────────────────────────────────────────────

using AdjVariant =
    annotation<std::variant<int, std::string, Basic>,
               schema::adjacently_tagged<"type", "value", "integer", "text", "basic">>;

struct AdjTaggedHolder {
    std::string name;
    AdjVariant data;

    auto operator==(const AdjTaggedHolder&) const -> bool = default;
};

// ── Variant with monostate ─────────────────────────────────────────

using ExtWithMono = annotation<std::variant<std::monostate, int, std::string>,
                               schema::externally_tagged<"none", "integer", "text">>;

TEST_SUITE(serde_simdjson_tagged_variant) {

// ── externally_tagged tests ────────────────────────────────────────

TEST_CASE(externally_tagged_int) {
    ExtVariant v = 42;
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"integer":42})");

    ExtVariant parsed;
    auto status = from_json(*encoded, parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(std::get<int>(parsed), 42);
}

TEST_CASE(externally_tagged_string) {
    ExtVariant v = std::string("hello");
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"text":"hello"})");

    ExtVariant parsed;
    auto status = from_json(*encoded, parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(std::get<std::string>(parsed), "hello");
}

TEST_CASE(externally_tagged_struct) {
    ExtVariant v = Basic{.is_valid = true, .i32 = 64};
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"basic":{"is_valid":true,"i32":64}})");

    ExtVariant parsed;
    auto status = from_json(*encoded, parsed);
    ASSERT_TRUE(status.has_value());
    auto& basic = std::get<Basic>(parsed);
    EXPECT_EQ(basic.is_valid, true);
    EXPECT_EQ(basic.i32, 64);
}

TEST_CASE(externally_tagged_in_struct) {
    ExtTaggedHolder input{.name = "test", .data = 42};
    auto encoded = to_json(input);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"name":"test","data":{"integer":42}})");

    ExtTaggedHolder parsed{};
    auto status = from_json(*encoded, parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(parsed, input);
}

TEST_CASE(externally_tagged_struct_roundtrip) {
    ExtTaggedHolder input{.name = "rtrip", .data = std::string("world")};
    auto encoded = to_json(input);
    ASSERT_TRUE(encoded.has_value());

    ExtTaggedHolder parsed{};
    auto status = from_json(*encoded, parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(parsed, input);
}

TEST_CASE(externally_tagged_monostate) {
    ExtWithMono v = std::monostate{};
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"none":null})");

    ExtWithMono parsed;
    parsed = 42;  // set to non-monostate first
    auto status = from_json(*encoded, parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_TRUE(std::holds_alternative<std::monostate>(parsed));
}

TEST_CASE(externally_tagged_unknown_tag_fails) {
    ExtVariant parsed;
    auto status = from_json(R"({"unknown":42})", parsed);
    EXPECT_FALSE(status.has_value());
}

// ── adjacently_tagged tests ────────────────────────────────────────

TEST_CASE(adjacently_tagged_int) {
    AdjVariant v = 42;
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"type":"integer","value":42})");

    AdjVariant parsed;
    auto status = from_json(*encoded, parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(std::get<int>(parsed), 42);
}

TEST_CASE(adjacently_tagged_string) {
    AdjVariant v = std::string("hello");
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"type":"text","value":"hello"})");

    AdjVariant parsed;
    auto status = from_json(*encoded, parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(std::get<std::string>(parsed), "hello");
}

TEST_CASE(adjacently_tagged_struct) {
    AdjVariant v = Basic{.is_valid = true, .i32 = 64};
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"type":"basic","value":{"is_valid":true,"i32":64}})");

    AdjVariant parsed;
    auto status = from_json(*encoded, parsed);
    ASSERT_TRUE(status.has_value());
    auto& basic = std::get<Basic>(parsed);
    EXPECT_EQ(basic.is_valid, true);
    EXPECT_EQ(basic.i32, 64);
}

TEST_CASE(adjacently_tagged_in_struct) {
    AdjTaggedHolder input{.name = "test", .data = 42};
    auto encoded = to_json(input);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"name":"test","data":{"type":"integer","value":42}})");

    AdjTaggedHolder parsed{};
    auto status = from_json(*encoded, parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(parsed, input);
}

TEST_CASE(adjacently_tagged_unknown_tag_fails) {
    AdjVariant parsed;
    auto status = from_json(R"({"type":"unknown","value":42})", parsed);
    EXPECT_FALSE(status.has_value());
}

};  // TEST_SUITE(serde_simdjson_tagged_variant)

}  // namespace

}  // namespace eventide::serde
