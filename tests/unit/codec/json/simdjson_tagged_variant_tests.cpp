#include <string>
#include <variant>

#include "fixtures/schema/common.h"
#include "fixtures/schema/tagged.h"
#include "kota/zest/zest.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

using namespace meta;

namespace {

using json::from_json;
using json::to_json;

using ShapeCircle = meta::fixtures::Circle;
using ShapeRect = meta::fixtures::Rect;
using Basic = meta::fixtures::BoolInt;

using ExtVariant = annotation<std::variant<int, std::string, Basic>,
                              meta::attrs::externally_tagged::names<"integer", "text", "basic">>;

struct ExtTaggedHolder {
    std::string name;
    ExtVariant data;
};

using AdjVariant =
    annotation<std::variant<int, std::string, Basic>,
               meta::attrs::adjacently_tagged<"type", "value">::names<"integer", "text", "basic">>;

struct AdjTaggedHolder {
    std::string name;
    AdjVariant data;
};

using ExtWithMono = annotation<std::variant<std::monostate, int, std::string>,
                               meta::attrs::externally_tagged::names<"none", "integer", "text">>;

struct ShapeLine {
    int line_width{};
};

using IntTagVariant = annotation<std::variant<ShapeCircle, ShapeRect>,
                                 meta::attrs::internally_tagged<"kind">::names<"circle", "rect">>;

using IntTagRenamedVariant =
    annotation<std::variant<ShapeLine, ShapeRect>,
               meta::attrs::internally_tagged<"kind">::names<"line", "rect">>;

// NB: named IntTagLabelHolder (not IntTagHolder) to avoid a GCC 14 bug where
// two anonymous-namespace types with the same name produce identical mangled
// names for template instantiations, causing the linker to merge unrelated
// weak symbols.  simdjson_variant_detail_tests.cpp also defines IntTagHolder.
// See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=70413
struct IntTagLabelHolder {
    std::string label;
    IntTagVariant shape;
};

struct camel_config {
    using field_rename = rename_policy::lower_camel;
};

TEST_SUITE(serde_simdjson_tagged_variant) {

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

TEST_CASE(internally_tagged_in_struct) {
    IntTagLabelHolder input{.label = "my shape", .shape = ShapeCircle{.radius = 5.0}};
    auto encoded = to_json(input);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"label":"my shape","shape":{"kind":"circle","radius":5.0}})");

    IntTagLabelHolder parsed{};
    auto status = from_json(*encoded, parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(parsed, input);
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

}  // namespace kota::codec
