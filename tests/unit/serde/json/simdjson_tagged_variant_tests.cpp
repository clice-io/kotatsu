#include <string>
#include <variant>

#include "eventide/zest/zest.h"
#include "eventide/serde/json/deserializer.h"
#include "eventide/serde/json/serializer.h"
#include "eventide/serde/serde/serde.h"

namespace eventide::serde {

using namespace refl;

namespace {

using json::from_json;
using json::to_json;

struct Basic {
    bool is_valid{};
    std::int32_t i32{};

    auto operator==(const Basic&) const -> bool = default;
};

// ── Externally tagged ──────────────────────────────────────────────

using ExtVariant = annotation<std::variant<int, std::string, Basic>,
                              refl::attrs::externally_tagged::names<"integer", "text", "basic">>;

struct ExtTaggedHolder {
    std::string name;
    ExtVariant data;

    auto operator==(const ExtTaggedHolder&) const -> bool = default;
};

// ── Adjacently tagged ──────────────────────────────────────────────

using AdjVariant =
    annotation<std::variant<int, std::string, Basic>,
               refl::attrs::adjacently_tagged<"type", "value">::names<"integer", "text", "basic">>;

struct AdjTaggedHolder {
    std::string name;
    AdjVariant data;

    auto operator==(const AdjTaggedHolder&) const -> bool = default;
};

// ── Variant with monostate ─────────────────────────────────────────

using ExtWithMono = annotation<std::variant<std::monostate, int, std::string>,
                               refl::attrs::externally_tagged::names<"none", "integer", "text">>;

// ── Internally tagged ─────────────────────────────────────────────

struct ShapeCircle {
    double radius{};

    auto operator==(const ShapeCircle&) const -> bool = default;
};

struct ShapeRect {
    double width{};
    double height{};

    auto operator==(const ShapeRect&) const -> bool = default;
};

struct ShapeLine {
    int line_width{};

    auto operator==(const ShapeLine&) const -> bool = default;
};

using IntTagVariant = annotation<std::variant<ShapeCircle, ShapeRect>,
                                 refl::attrs::internally_tagged<"kind">::names<"circle", "rect">>;

using IntTagRenamedVariant =
    annotation<std::variant<ShapeLine, ShapeRect>,
               refl::attrs::internally_tagged<"kind">::names<"line", "rect">>;

using AdjShapeVariant =
    annotation<std::variant<ShapeCircle, ShapeRect>,
               refl::attrs::adjacently_tagged<"type", "value">::names<"circle", "rect">>;

struct IntTagHolder {
    std::string label;
    IntTagVariant shape;

    auto operator==(const IntTagHolder&) const -> bool = default;
};

struct camel_config {
    using field_rename = rename_policy::lower_camel;
};

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

TEST_CASE(adjacently_tagged_content_before_tag) {
    AdjVariant parsed;
    auto status = from_json(R"({"value":"hello","type":"text"})", parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(std::get<std::string>(parsed), "hello");
}

TEST_CASE(adjacently_tagged_content_before_tag_ambiguous_object) {
    AdjShapeVariant parsed;
    auto status = from_json(R"({"value":{"width":10.0,"height":20.0},"type":"rect"})", parsed);
    ASSERT_TRUE(status.has_value());

    auto& rect = std::get<ShapeRect>(parsed);
    EXPECT_EQ(rect.width, 10.0);
    EXPECT_EQ(rect.height, 20.0);
}

TEST_CASE(adjacently_tagged_unknown_tag_fails) {
    AdjVariant parsed;
    auto status = from_json(R"({"type":"unknown","value":42})", parsed);
    EXPECT_FALSE(status.has_value());
}

// ── internally_tagged tests ───────────────────────────────────────

TEST_CASE(internally_tagged_circle_serialize) {
    IntTagVariant v = ShapeCircle{.radius = 3.14};
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"kind":"circle","radius":3.14})");
}

TEST_CASE(internally_tagged_rect_serialize) {
    IntTagVariant v = ShapeRect{.width = 10.0, .height = 20.0};
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"kind":"rect","width":10.0,"height":20.0})");
}

TEST_CASE(internally_tagged_circle_roundtrip) {
    IntTagVariant v = ShapeCircle{.radius = 3.14};
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());

    IntTagVariant parsed;
    auto status = from_json(*encoded, parsed);
    ASSERT_TRUE(status.has_value());
    auto& circle = std::get<ShapeCircle>(parsed);
    EXPECT_EQ(circle.radius, 3.14);
}

TEST_CASE(internally_tagged_rect_roundtrip) {
    IntTagVariant v = ShapeRect{.width = 10.0, .height = 20.0};
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());

    IntTagVariant parsed;
    auto status = from_json(*encoded, parsed);
    ASSERT_TRUE(status.has_value());
    auto& rect = std::get<ShapeRect>(parsed);
    EXPECT_EQ(rect.width, 10.0);
    EXPECT_EQ(rect.height, 20.0);
}

TEST_CASE(internally_tagged_in_struct) {
    IntTagHolder input{.label = "my shape", .shape = ShapeCircle{.radius = 5.0}};
    auto encoded = to_json(input);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"label":"my shape","shape":{"kind":"circle","radius":5.0}})");

    IntTagHolder parsed{};
    auto status = from_json(*encoded, parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(parsed, input);
}

TEST_CASE(internally_tagged_unknown_tag_fails) {
    IntTagVariant parsed;
    auto status = from_json(R"({"kind":"triangle","side":5})", parsed);
    EXPECT_FALSE(status.has_value());
}

TEST_CASE(internally_tagged_missing_tag_fails) {
    IntTagVariant parsed;
    auto status = from_json(R"({"radius":5})", parsed);
    EXPECT_FALSE(status.has_value());
}

TEST_CASE(internally_tagged_deserialize_respects_config_rename) {
    IntTagRenamedVariant parsed{};
    auto status = from_json<camel_config>(R"({"kind":"line","lineWidth":7})", parsed);
    ASSERT_TRUE(status.has_value());
    ASSERT_TRUE(std::holds_alternative<ShapeLine>(parsed));
    EXPECT_EQ(std::get<ShapeLine>(parsed).line_width, 7);
}

};  // TEST_SUITE(serde_simdjson_tagged_variant)

}  // namespace

}  // namespace eventide::serde
