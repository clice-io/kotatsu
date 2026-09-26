#include <string>
#include <variant>

#include "fixtures/schema/common.h"
#include "fixtures/schema/tagged.h"
#include "kota/zest/zest.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

using namespace meta;

namespace {

using json::from_string;
using json::to_string;

using ShapeCircle = meta::fixtures::Circle;
using ShapeRect = meta::fixtures::Rect;
using Basic = meta::fixtures::BoolInt;

KOTATSU_ANNOTATION(ext_variant_annotation, tagged = true, tag_names = {"integer", "text", "basic"});
using ExtVariant = annotate<ext_variant_annotation>::type<std::variant<int, std::string, Basic>>;

struct ExtTaggedHolder {
    std::string name;
    ExtVariant data;
};

KOTATSU_ANNOTATION(adj_variant_annotation,
                   tag = "type",
                   content = "value",
                   tag_names = {"integer", "text", "basic"});
using AdjVariant = annotate<adj_variant_annotation>::type<std::variant<int, std::string, Basic>>;

struct AdjTaggedHolder {
    std::string name;
    AdjVariant data;
};

KOTATSU_ANNOTATION(ext_with_mono_annotation,
                   tagged = true,
                   tag_names = {"none", "integer", "text"});
using ExtWithMono =
    annotate<ext_with_mono_annotation>::type<std::variant<std::monostate, int, std::string>>;

struct ShapeLine {
    int line_width{};
};

KOTATSU_ANNOTATION(int_tag_variant_annotation, tag = "kind", tag_names = {"circle", "rect"});
using IntTagVariant =
    annotate<int_tag_variant_annotation>::type<std::variant<ShapeCircle, ShapeRect>>;

KOTATSU_ANNOTATION(int_tag_renamed_variant_annotation, tag = "kind", tag_names = {"line", "rect"});
using IntTagRenamedVariant =
    annotate<int_tag_renamed_variant_annotation>::type<std::variant<ShapeLine, ShapeRect>>;

struct IntTagHolder {
    std::string label;
    IntTagVariant shape;
};

struct camel_config {
    using field_rename = rename_policy::lower_camel;
};

ZEST_SUITE(codec_json_simdjson_tagged_variant) {

ZEST_CASE(externally_tagged_int) {
    ExtVariant v = 42;
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"integer":42})");

    ExtVariant parsed;
    auto status = from_string(*encoded, parsed);
    ASSERT(status);
    EXPECT(std::get<int>(parsed) == 42);
}

ZEST_CASE(externally_tagged_string) {
    ExtVariant v = std::string("hello");
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"text":"hello"})");

    ExtVariant parsed;
    auto status = from_string(*encoded, parsed);
    ASSERT(status);
    EXPECT(std::get<std::string>(parsed) == "hello");
}

ZEST_CASE(externally_tagged_struct) {
    ExtVariant v = Basic{.is_valid = true, .i32 = 64};
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"basic":{"is_valid":true,"i32":64}})");

    ExtVariant parsed;
    auto status = from_string(*encoded, parsed);
    ASSERT(status);
    auto& basic = std::get<Basic>(parsed);
    EXPECT(basic.is_valid == true);
    EXPECT(basic.i32 == 64);
}

ZEST_CASE(externally_tagged_in_struct) {
    ExtTaggedHolder input{.name = "test", .data = 42};
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"name":"test","data":{"integer":42}})");

    ExtTaggedHolder parsed{};
    auto status = from_string(*encoded, parsed);
    ASSERT(status);
    EXPECT(parsed == input);
}

ZEST_CASE(externally_tagged_monostate) {
    ExtWithMono v = std::monostate{};
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"none":null})");

    ExtWithMono parsed;
    parsed = 42;  // set to non-monostate first
    auto status = from_string(*encoded, parsed);
    ASSERT(status);
    EXPECT(std::holds_alternative<std::monostate>(parsed));
}

ZEST_CASE(adjacently_tagged_int) {
    AdjVariant v = 42;
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"type":"integer","value":42})");

    AdjVariant parsed;
    auto status = from_string(*encoded, parsed);
    ASSERT(status);
    EXPECT(std::get<int>(parsed) == 42);
}

ZEST_CASE(adjacently_tagged_string) {
    AdjVariant v = std::string("hello");
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"type":"text","value":"hello"})");

    AdjVariant parsed;
    auto status = from_string(*encoded, parsed);
    ASSERT(status);
    EXPECT(std::get<std::string>(parsed) == "hello");
}

ZEST_CASE(adjacently_tagged_struct) {
    AdjVariant v = Basic{.is_valid = true, .i32 = 64};
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"type":"basic","value":{"is_valid":true,"i32":64}})");

    AdjVariant parsed;
    auto status = from_string(*encoded, parsed);
    ASSERT(status);
    auto& basic = std::get<Basic>(parsed);
    EXPECT(basic.is_valid == true);
    EXPECT(basic.i32 == 64);
}

ZEST_CASE(adjacently_tagged_in_struct) {
    AdjTaggedHolder input{.name = "test", .data = 42};
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"name":"test","data":{"type":"integer","value":42}})");

    AdjTaggedHolder parsed{};
    auto status = from_string(*encoded, parsed);
    ASSERT(status);
    EXPECT(parsed == input);
}

ZEST_CASE(internally_tagged_circle_serialize) {
    IntTagVariant v = ShapeCircle{.radius = 3.14};
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"kind":"circle","radius":3.14})");
}

ZEST_CASE(internally_tagged_rect_serialize) {
    IntTagVariant v = ShapeRect{.width = 10.0, .height = 20.0};
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"kind":"rect","width":10.0,"height":20.0})");
}

ZEST_CASE(internally_tagged_in_struct) {
    IntTagHolder input{.label = "my shape", .shape = ShapeCircle{.radius = 5.0}};
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"label":"my shape","shape":{"kind":"circle","radius":5.0}})");

    IntTagHolder parsed{};
    auto status = from_string(*encoded, parsed);
    ASSERT(status);
    EXPECT(parsed == input);
}

ZEST_CASE(internally_tagged_deserialize_respects_config_rename) {
    IntTagRenamedVariant parsed{};
    auto status = from_string<camel_config>(R"({"kind":"line","lineWidth":7})", parsed);
    ASSERT(status);
    ASSERT(std::holds_alternative<ShapeLine>(parsed));
    EXPECT(std::get<ShapeLine>(parsed).line_width == 7);
}

};  // ZEST_SUITE(codec_json_simdjson_tagged_variant)

}  // namespace

}  // namespace kota::codec
