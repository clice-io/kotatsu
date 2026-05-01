#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "fixtures/schema/common.h"
#include "fixtures/schema/primitives.h"
#include "fixtures/schema/tagged.h"
#include "kota/zest/zest.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

using namespace meta;

namespace {

using json::from_json;
using json::to_json;

using Point = meta::fixtures::Point2d;
using Color = meta::fixtures::Color3;
using IntHolder = meta::fixtures::IntHolder;
using StringHolder = meta::fixtures::StringHolder;

using ExtSimple =
    annotation<std::variant<int, std::string>, meta::attrs::externally_tagged::names<"num", "str">>;

using ExtWithMono = annotation<std::variant<std::monostate, int, std::string>,
                               meta::attrs::externally_tagged::names<"none", "num", "str">>;

using ExtWithStruct = annotation<std::variant<int, Point, Color>,
                                 meta::attrs::externally_tagged::names<"int", "point", "color">>;

using AdjSimple = annotation<std::variant<int, std::string>,
                             meta::attrs::adjacently_tagged<"t", "v">::names<"num", "str">>;

using AdjWithMono =
    annotation<std::variant<std::monostate, int, std::string>,
               meta::attrs::adjacently_tagged<"tag", "data">::names<"nil", "num", "str">>;

using AdjWithStruct =
    annotation<std::variant<int, Point>,
               meta::attrs::adjacently_tagged<"type", "value">::names<"int", "point">>;

using Circle = meta::fixtures::Circle;
using Rect = meta::fixtures::Rect;
using Triangle = meta::fixtures::Triangle;

using IntTagShape = annotation<std::variant<Circle, Rect>,
                               meta::attrs::internally_tagged<"type">::names<"circle", "rect">>;

using IntTagTriShape =
    annotation<std::variant<Circle, Rect, Triangle>,
               meta::attrs::internally_tagged<"kind">::names<"circle", "rect", "triangle">>;

struct ExtHolder {
    std::string label;
    ExtWithStruct item;
};

struct AdjHolder {
    std::string name;
    AdjSimple data;
};

struct IntTagHolder {
    std::string name;
    IntTagShape shape;
};

TEST_SUITE(serde_variant_untagged) {

TEST_CASE(bool_vs_int) {
    using V = std::variant<bool, int>;

    V v_bool = true;
    ASSERT_EQ(to_json(v_bool), "true");

    V v_int = 42;
    ASSERT_EQ(to_json(v_int), "42");

    // bool JSON → bool alternative (not int)
    V out{};
    ASSERT_TRUE(from_json("true", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<bool>(out), true);

    ASSERT_TRUE(from_json("false", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<bool>(out), false);

    // integer JSON → int alternative (not bool)
    ASSERT_TRUE(from_json("7", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<int>(out), 7);
}

TEST_CASE(int_before_double) {
    // When int comes before double, integer JSON should match int (first match wins)
    using V = std::variant<int, double>;

    V out{};
    ASSERT_TRUE(from_json("42", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<int>(out), 42);

    // Floating-point JSON can only match double
    ASSERT_TRUE(from_json("3.14", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<double>(out), 3.14);
}

TEST_CASE(double_before_int) {
    // Even when double comes first, integer JSON matches int (more precise kind match).
    // Floating-point JSON still matches double.
    using V = std::variant<double, int>;

    V out{};
    ASSERT_TRUE(from_json("42", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<int>(out), 42);

    ASSERT_TRUE(from_json("3.14", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<double>(out), 3.14);
}

TEST_CASE(monostate_matches_null) {
    using V = std::variant<std::monostate, int, std::string>;

    V out = 42;  // start non-null
    ASSERT_TRUE(from_json("null", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(out));
}

TEST_CASE(string_vs_int) {
    using V = std::variant<std::string, int>;

    V out{};
    ASSERT_TRUE(from_json(R"("hello")", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<std::string>(out), "hello");

    ASSERT_TRUE(from_json("99", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<int>(out), 99);
}

TEST_CASE(array_vs_object) {
    using V = std::variant<std::vector<int>, std::map<std::string, int>>;

    V out{};
    ASSERT_TRUE(from_json("[1,2,3]", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<std::vector<int>>(out), std::vector<int>({1, 2, 3}));

    ASSERT_TRUE(from_json(R"({"a":1,"b":2})", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    auto& m = std::get<std::map<std::string, int>>(out);
    EXPECT_EQ(m.size(), 2U);
    EXPECT_EQ(m["a"], 1);
    EXPECT_EQ(m["b"], 2);
}

TEST_CASE(scalar_vs_array_vs_object) {
    using V = std::variant<int, std::vector<int>, Point>;

    V out{};
    ASSERT_TRUE(from_json("5", out).has_value());
    EXPECT_EQ(out.index(), 0U);

    ASSERT_TRUE(from_json("[1,2]", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<std::vector<int>>(out), std::vector<int>({1, 2}));

    ASSERT_TRUE(from_json(R"({"x":1.0,"y":2.0})", out).has_value());
    EXPECT_EQ(out.index(), 2U);
    EXPECT_EQ(std::get<Point>(out), (Point{1.0, 2.0}));
}

TEST_CASE(single_alternative) {
    using V = std::variant<int>;

    V v = 42;
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, "42");

    V out{};
    ASSERT_TRUE(from_json("99", out).has_value());
    EXPECT_EQ(std::get<int>(out), 99);
}

TEST_CASE(struct_deep_scoring) {
    // Two struct types with the same field name but different field types.
    // Deep scoring matches the correct alternative by recursively comparing field types.
    using V = std::variant<IntHolder, StringHolder>;

    V out{};
    // Deep scoring: "hello" is string → StringHolder.value (string) scores higher than
    // IntHolder.value (int)
    ASSERT_TRUE(from_json(R"({"value":"hello"})", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<StringHolder>(out).value, "hello");

    // Deep scoring: 42 is int → IntHolder.value (int) scores higher than
    // StringHolder.value (string)
    ASSERT_TRUE(from_json(R"({"value":42})", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<IntHolder>(out).value, 42);
}

TEST_CASE(no_matching_type_fails) {
    using V = std::variant<int, std::string>;

    V out{};
    // JSON array doesn't match int or string
    EXPECT_FALSE(from_json("[1]", out).has_value());

    // JSON object doesn't match int or string
    EXPECT_FALSE(from_json(R"({"a":1})", out).has_value());

    // JSON bool doesn't match int or string
    EXPECT_FALSE(from_json("true", out).has_value());

    // JSON null doesn't match int or string
    EXPECT_FALSE(from_json("null", out).has_value());
}

TEST_CASE(variant_roundtrip_all_scalars) {
    using V = std::variant<std::monostate, bool, int, double, std::string>;

    auto check = [](V input) -> bool {
        auto encoded = to_json(input);
        if(!encoded.has_value())
            return false;
        V out{};
        auto status = from_json(*encoded, out);
        if(!status.has_value())
            return false;
        return out.index() == input.index();
    };

    EXPECT_TRUE(check(std::monostate{}));
    EXPECT_TRUE(check(true));
    EXPECT_TRUE(check(42));
    EXPECT_TRUE(check(3.14));
    EXPECT_TRUE(check(std::string("test")));
}

TEST_CASE(nested_variant) {
    using Inner = std::variant<int, std::string>;
    using Outer = std::variant<Inner, double>;

    Outer out{};
    // integer → Inner accepts int, double accepts int; Inner is variant so recurse
    // Inner's int alternative matches exactly → selects Inner
    ASSERT_TRUE(from_json("42", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    auto& inner = std::get<Inner>(out);
    EXPECT_EQ(inner.index(), 0U);
    EXPECT_EQ(std::get<int>(inner), 42);

    // string → only Inner accepts string
    ASSERT_TRUE(from_json(R"("hello")", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    auto& inner2 = std::get<Inner>(out);
    EXPECT_EQ(inner2.index(), 1U);
    EXPECT_EQ(std::get<std::string>(inner2), "hello");

    // floating → double accepts float, Inner's int doesn't, Inner's string doesn't
    ASSERT_TRUE(from_json("3.14", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<double>(out), 3.14);
}

TEST_CASE(nested_variant_no_match_falls_through) {
    using Inner = std::variant<int, std::string>;
    using Outer = std::variant<Inner, bool>;

    Outer out{};
    // bool → Inner doesn't accept bool (int doesn't, string doesn't), bool does
    ASSERT_TRUE(from_json("true", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<bool>(out), true);

    // object → neither Inner nor bool accepts object
    EXPECT_FALSE(from_json(R"({"x":1})", out).has_value());
}

TEST_CASE(int64_vs_uint64) {
    using V = std::variant<std::int64_t, std::uint64_t>;

    V out{};
    // positive integer within int64 range → both accept, but numeric tiebreaker
    // should prefer the exact source kind
    ASSERT_TRUE(from_json("42", out).has_value());
    // simdjson parses 42 as signed_integer → int64
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<std::int64_t>(out), 42);

    // large unsigned → simdjson parses as unsigned_integer → uint64
    ASSERT_TRUE(from_json("18446744073709551615", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<std::uint64_t>(out), UINT64_MAX);
}

TEST_CASE(uint64_before_int64) {
    using V = std::variant<std::uint64_t, std::int64_t>;

    V out{};
    // large unsigned → uint64
    ASSERT_TRUE(from_json("18446744073709551615", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<std::uint64_t>(out), UINT64_MAX);

    // negative → only int64 accepts
    ASSERT_TRUE(from_json("-1", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<std::int64_t>(out), -1);
}

TEST_CASE(optional_variant) {
    using V = std::variant<int, std::string>;
    using OV = std::optional<V>;

    OV out{};
    ASSERT_TRUE(from_json("null", out).has_value());
    EXPECT_FALSE(out.has_value());

    ASSERT_TRUE(from_json("42", out).has_value());
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->index(), 0U);
    EXPECT_EQ(std::get<int>(*out), 42);

    ASSERT_TRUE(from_json(R"("test")", out).has_value());
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->index(), 1U);
    EXPECT_EQ(std::get<std::string>(*out), "test");
}

};  // TEST_SUITE(serde_variant_untagged)

TEST_SUITE(serde_variant_ext) {

TEST_CASE(roundtrip_all_alternatives) {
    {
        ExtWithStruct v = 42;
        auto encoded = to_json(v);
        ASSERT_TRUE(encoded.has_value());
        EXPECT_EQ(*encoded, R"({"int":42})");

        ExtWithStruct out{};
        ASSERT_TRUE(from_json(*encoded, out).has_value());
        EXPECT_EQ(std::get<int>(out), 42);
    }
    {
        ExtWithStruct v = Point{.x = 1.5, .y = 2.5};
        auto encoded = to_json(v);
        ASSERT_TRUE(encoded.has_value());
        EXPECT_EQ(*encoded, R"({"point":{"x":1.5,"y":2.5}})");

        ExtWithStruct out{};
        ASSERT_TRUE(from_json(*encoded, out).has_value());
        EXPECT_EQ(std::get<Point>(out), (Point{1.5, 2.5}));
    }
    {
        ExtWithStruct v = Color{.r = 255, .g = 128, .b = 0};
        auto encoded = to_json(v);
        ASSERT_TRUE(encoded.has_value());
        EXPECT_EQ(*encoded, R"({"color":{"r":255,"g":128,"b":0}})");

        ExtWithStruct out{};
        ASSERT_TRUE(from_json(*encoded, out).has_value());
        EXPECT_EQ(std::get<Color>(out), (Color{255, 128, 0}));
    }
}

TEST_CASE(monostate_roundtrip) {
    ExtWithMono v_none = std::monostate{};
    auto enc = to_json(v_none);
    ASSERT_TRUE(enc.has_value());
    EXPECT_EQ(*enc, R"({"none":null})");

    ExtWithMono out = 42;
    ASSERT_TRUE(from_json(*enc, out).has_value());
    EXPECT_TRUE(std::holds_alternative<std::monostate>(out));

    ExtWithMono v_int = 7;
    enc = to_json(v_int);
    ASSERT_TRUE(enc.has_value());
    EXPECT_EQ(*enc, R"({"num":7})");

    ASSERT_TRUE(from_json(*enc, out).has_value());
    EXPECT_EQ(std::get<int>(out), 7);
}

TEST_CASE(unknown_tag_fails) {
    ExtSimple out{};
    EXPECT_FALSE(from_json(R"({"bad":42})", out).has_value());
}

TEST_CASE(empty_object_fails) {
    ExtSimple out{};
    EXPECT_FALSE(from_json(R"({})", out).has_value());
}

TEST_CASE(not_an_object_fails) {
    ExtSimple out{};
    EXPECT_FALSE(from_json("42", out).has_value());
    EXPECT_FALSE(from_json(R"("str")", out).has_value());
    EXPECT_FALSE(from_json("[1]", out).has_value());
    EXPECT_FALSE(from_json("null", out).has_value());
}

TEST_CASE(in_holder_struct) {
    ExtHolder input{
        .label = "origin",
        .item = Point{.x = 0.0, .y = 0.0}
    };
    auto encoded = to_json(input);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"label":"origin","item":{"point":{"x":0.0,"y":0.0}}})");

    ExtHolder out{};
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    EXPECT_EQ(out, input);
}

TEST_CASE(in_vector) {
    std::vector<ExtSimple> vec = {ExtSimple{42}, ExtSimple{std::string("hi")}};
    auto encoded = to_json(vec);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"([{"num":42},{"str":"hi"}])");

    std::vector<ExtSimple> out;
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    ASSERT_EQ(out.size(), 2U);
    EXPECT_EQ(std::get<int>(out[0]), 42);
    EXPECT_EQ(std::get<std::string>(out[1]), "hi");
}

TEST_CASE(in_optional) {
    std::optional<ExtSimple> present = ExtSimple{std::string("val")};
    auto encoded = to_json(present);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"str":"val"})");

    std::optional<ExtSimple> out;
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(std::get<std::string>(*out), "val");

    std::optional<ExtSimple> absent;
    encoded = to_json(absent);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, "null");

    ASSERT_TRUE(from_json("null", out).has_value());
    EXPECT_FALSE(out.has_value());
}

TEST_CASE(empty_string_value) {
    ExtSimple v = std::string("");
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"str":""})");

    ExtSimple out{};
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    EXPECT_EQ(std::get<std::string>(out), "");
}

};  // TEST_SUITE(serde_variant_ext)

TEST_SUITE(serde_variant_adj) {

TEST_CASE(roundtrip_int) {
    AdjSimple v = 99;
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"t":"num","v":99})");

    AdjSimple out{};
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    EXPECT_EQ(std::get<int>(out), 99);
}

TEST_CASE(roundtrip_string) {
    AdjSimple v = std::string("abc");
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"t":"str","v":"abc"})");

    AdjSimple out{};
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    EXPECT_EQ(std::get<std::string>(out), "abc");
}

TEST_CASE(monostate) {
    AdjWithMono v = std::monostate{};
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"tag":"nil","data":null})");

    AdjWithMono out = 42;
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    EXPECT_TRUE(std::holds_alternative<std::monostate>(out));
}

TEST_CASE(content_before_tag) {
    // Value field appears before tag field — requires buffering
    AdjSimple out{};
    ASSERT_TRUE(from_json(R"({"v":42,"t":"num"})", out).has_value());
    EXPECT_EQ(std::get<int>(out), 42);

    ASSERT_TRUE(from_json(R"({"v":"hello","t":"str"})", out).has_value());
    EXPECT_EQ(std::get<std::string>(out), "hello");
}

TEST_CASE(content_before_tag_struct) {
    // Struct content buffered before tag is known
    AdjWithStruct out{};
    ASSERT_TRUE(from_json(R"({"value":{"x":1.0,"y":2.0},"type":"point"})", out).has_value());
    EXPECT_EQ(std::get<Point>(out), (Point{1.0, 2.0}));
}

TEST_CASE(extra_unknown_fields_ignored) {
    // Unknown fields alongside tag and content should be skipped
    AdjSimple out{};
    ASSERT_TRUE(from_json(R"({"t":"num","extra":true,"v":5,"other":"x"})", out).has_value());
    EXPECT_EQ(std::get<int>(out), 5);
}

TEST_CASE(missing_tag_fails) {
    AdjSimple out{};
    // Only content, no tag
    EXPECT_FALSE(from_json(R"({"v":42})", out).has_value());
}

TEST_CASE(missing_content_fails) {
    AdjSimple out{};
    // Only tag, no content
    EXPECT_FALSE(from_json(R"({"t":"num"})", out).has_value());
}

TEST_CASE(unknown_tag_value_fails) {
    AdjSimple out{};
    EXPECT_FALSE(from_json(R"({"t":"unknown","v":42})", out).has_value());
}

TEST_CASE(duplicate_tag_fails) {
    AdjSimple out{};
    EXPECT_FALSE(from_json(R"({"t":"num","t":"str","v":42})", out).has_value());
}

TEST_CASE(duplicate_content_fails) {
    AdjSimple out{};
    EXPECT_FALSE(from_json(R"({"t":"num","v":1,"v":2})", out).has_value());
}

TEST_CASE(empty_object_fails) {
    AdjSimple out{};
    EXPECT_FALSE(from_json(R"({})", out).has_value());
}

TEST_CASE(not_an_object_fails) {
    AdjSimple out{};
    EXPECT_FALSE(from_json("42", out).has_value());
    EXPECT_FALSE(from_json(R"("str")", out).has_value());
    EXPECT_FALSE(from_json("[1]", out).has_value());
}

TEST_CASE(in_holder_struct) {
    AdjHolder input{.name = "test", .data = 42};
    auto encoded = to_json(input);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"name":"test","data":{"t":"num","v":42}})");

    AdjHolder out{};
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    EXPECT_EQ(out, input);
}

TEST_CASE(in_vector) {
    std::vector<AdjSimple> vec = {AdjSimple{1}, AdjSimple{std::string("x")}};
    auto encoded = to_json(vec);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"([{"t":"num","v":1},{"t":"str","v":"x"}])");

    std::vector<AdjSimple> out;
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    ASSERT_EQ(out.size(), 2U);
    EXPECT_EQ(std::get<int>(out[0]), 1);
    EXPECT_EQ(std::get<std::string>(out[1]), "x");
}

TEST_CASE(in_map) {
    std::map<std::string, AdjSimple> m;
    m["a"] = AdjSimple{10};
    m["b"] = AdjSimple{std::string("val")};
    auto encoded = to_json(m);
    ASSERT_TRUE(encoded.has_value());

    std::map<std::string, AdjSimple> out;
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    ASSERT_EQ(out.size(), 2U);
    EXPECT_EQ(std::get<int>(out["a"]), 10);
    EXPECT_EQ(std::get<std::string>(out["b"]), "val");
}

};  // TEST_SUITE(serde_variant_adj)

TEST_SUITE(serde_variant_int_tag) {

TEST_CASE(circle_roundtrip) {
    IntTagShape v = Circle{.radius = 5.0};
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"type":"circle","radius":5.0})");

    IntTagShape out{};
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    EXPECT_EQ(std::get<Circle>(out), (Circle{.radius = 5.0}));
}

TEST_CASE(rect_roundtrip) {
    IntTagShape v = Rect{.width = 3.0, .height = 4.0};
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"type":"rect","width":3.0,"height":4.0})");

    IntTagShape out{};
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    EXPECT_EQ(std::get<Rect>(out), (Rect{3.0, 4.0}));
}

TEST_CASE(tag_not_first_in_input) {
    // Tag field comes after other fields — deserialization via DOM should still work
    IntTagShape out{};
    ASSERT_TRUE(from_json(R"({"radius":2.5,"type":"circle"})", out).has_value());
    EXPECT_EQ(std::get<Circle>(out), (Circle{.radius = 2.5}));
}

TEST_CASE(extra_fields_ignored) {
    // Extra fields in the object should be silently ignored
    IntTagShape out{};
    ASSERT_TRUE(from_json(R"({"type":"circle","radius":1.0,"extra":"ignored"})", out).has_value());
    EXPECT_EQ(std::get<Circle>(out), (Circle{.radius = 1.0}));
}

TEST_CASE(three_alternatives) {
    IntTagTriShape v1 = Circle{.radius = 1.0};
    IntTagTriShape v2 = Rect{.width = 2.0, .height = 3.0};
    IntTagTriShape v3 = Triangle{.base = 4.0, .height = 5.0};

    auto e1 = to_json(v1);
    auto e2 = to_json(v2);
    auto e3 = to_json(v3);
    ASSERT_TRUE(e1.has_value());
    ASSERT_TRUE(e2.has_value());
    ASSERT_TRUE(e3.has_value());
    EXPECT_EQ(*e1, R"({"kind":"circle","radius":1.0})");
    EXPECT_EQ(*e2, R"({"kind":"rect","width":2.0,"height":3.0})");
    EXPECT_EQ(*e3, R"({"kind":"triangle","base":4.0,"height":5.0})");

    IntTagTriShape out{};
    ASSERT_TRUE(from_json(*e1, out).has_value());
    EXPECT_EQ(std::get<Circle>(out).radius, 1.0);

    ASSERT_TRUE(from_json(*e2, out).has_value());
    EXPECT_EQ(std::get<Rect>(out), (Rect{2.0, 3.0}));

    ASSERT_TRUE(from_json(*e3, out).has_value());
    EXPECT_EQ(std::get<Triangle>(out), (Triangle{4.0, 5.0}));
}

TEST_CASE(unknown_tag_fails) {
    IntTagShape out{};
    EXPECT_FALSE(from_json(R"({"type":"pentagon","sides":5})", out).has_value());
}

TEST_CASE(missing_tag_fails) {
    IntTagShape out{};
    EXPECT_FALSE(from_json(R"({"radius":5.0})", out).has_value());
}

TEST_CASE(tag_not_a_string_fails) {
    IntTagShape out{};
    EXPECT_FALSE(from_json(R"({"type":1,"radius":5.0})", out).has_value());
}

TEST_CASE(not_an_object_fails) {
    IntTagShape out{};
    EXPECT_FALSE(from_json("42", out).has_value());
    EXPECT_FALSE(from_json(R"("circle")", out).has_value());
    EXPECT_FALSE(from_json("[1]", out).has_value());
    EXPECT_FALSE(from_json("null", out).has_value());
}

TEST_CASE(in_holder_struct) {
    IntTagHolder input{.name = "shape1", .shape = Circle{.radius = 9.0}};
    auto encoded = to_json(input);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"name":"shape1","shape":{"type":"circle","radius":9.0}})");

    IntTagHolder out{};
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    EXPECT_EQ(out, input);
}

TEST_CASE(in_vector) {
    std::vector<IntTagShape> shapes = {
        IntTagShape{Circle{.radius = 1.0}},
        IntTagShape{Rect{.width = 2.0, .height = 3.0}},
    };
    auto encoded = to_json(shapes);
    ASSERT_TRUE(encoded.has_value());

    std::vector<IntTagShape> out;
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    ASSERT_EQ(out.size(), 2U);
    EXPECT_EQ(std::get<Circle>(out[0]).radius, 1.0);
    EXPECT_EQ(std::get<Rect>(out[1]), (Rect{2.0, 3.0}));
}

TEST_CASE(in_optional) {
    std::optional<IntTagShape> present = IntTagShape{
        Rect{.width = 1.0, .height = 2.0}
    };
    auto encoded = to_json(present);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"type":"rect","width":1.0,"height":2.0})");

    std::optional<IntTagShape> out;
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(std::get<Rect>(*out), (Rect{1.0, 2.0}));

    ASSERT_TRUE(from_json("null", out).has_value());
    EXPECT_FALSE(out.has_value());
}

TEST_CASE(missing_required_field_rejects_tagged_candidate) {
    // Missing non-optional field in a tagged variant should fail deserialization
    IntTagShape out{};
    auto result = from_json(R"({"type":"rect","width":5.0})", out);
    EXPECT_FALSE(result.has_value());
}

TEST_CASE(missing_required_field_rejects_untagged_variant_candidate) {
    // In untagged variant probing, a missing required field should reject
    // the candidate and try the next alternative.
    // Circle has field "radius", Rect has "width" + "height".
    // {"width": 5.0} is missing "height" so Rect should be rejected.
    using UntaggedShape = std::variant<Rect, Circle>;
    UntaggedShape out{};
    // This should fail because Rect needs both width+height, and Circle
    // doesn't match either (no "radius" field).
    auto result = from_json(R"({"width":5.0})", out);
    EXPECT_FALSE(result.has_value());
}

};  // TEST_SUITE(serde_variant_int_tag)

TEST_SUITE(serde_variant_nested) {

TEST_CASE(variant_in_struct_in_variant) {
    // An externally tagged variant whose struct alternative contains another ext variant
    using Inner =
        annotation<std::variant<int, std::string>, meta::attrs::externally_tagged::names<"i", "s">>;

    struct Wrapper {
        std::string id;
        Inner val;
    };

    using Outer = annotation<std::variant<int, Wrapper>,
                             meta::attrs::externally_tagged::names<"plain", "wrapped">>;

    Outer v = Wrapper{.id = "w1", .val = std::string("inner")};
    auto encoded = to_json(v);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"wrapped":{"id":"w1","val":{"s":"inner"}}})");

    Outer out{};
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    EXPECT_EQ(out, v);
}

TEST_CASE(vector_of_tagged_variants) {
    using V = annotation<std::variant<int, std::string>,
                         meta::attrs::adjacently_tagged<"t", "v">::names<"i", "s">>;

    std::vector<V> vec = {V{1}, V{std::string("a")}, V{2}, V{std::string("b")}};
    auto encoded = to_json(vec);
    ASSERT_TRUE(encoded.has_value());

    std::vector<V> out;
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    ASSERT_EQ(out.size(), 4U);
    EXPECT_EQ(std::get<int>(out[0]), 1);
    EXPECT_EQ(std::get<std::string>(out[1]), "a");
    EXPECT_EQ(std::get<int>(out[2]), 2);
    EXPECT_EQ(std::get<std::string>(out[3]), "b");
}

TEST_CASE(map_of_internally_tagged) {
    std::map<std::string, IntTagShape> shapes;
    shapes["c"] = IntTagShape{Circle{.radius = 1.0}};
    shapes["r"] = IntTagShape{
        Rect{.width = 2.0, .height = 3.0}
    };

    auto encoded = to_json(shapes);
    ASSERT_TRUE(encoded.has_value());

    std::map<std::string, IntTagShape> out;
    ASSERT_TRUE(from_json(*encoded, out).has_value());
    ASSERT_EQ(out.size(), 2U);
    EXPECT_EQ(std::get<Circle>(out["c"]).radius, 1.0);
    EXPECT_EQ(std::get<Rect>(out["r"]), (Rect{2.0, 3.0}));
}

TEST_CASE(optional_tagged_absent) {
    std::optional<ExtSimple> absent;
    auto encoded = to_json(absent);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, "null");

    std::optional<ExtSimple> out = ExtSimple{42};
    ASSERT_TRUE(from_json("null", out).has_value());
    EXPECT_FALSE(out.has_value());
}

};  // TEST_SUITE(serde_variant_nested)

TEST_SUITE(serde_variant_deep_dispatch) {

TEST_CASE(struct_with_variant_field_disambiguation) {
    // Two structs whose variant-typed fields accept different source kinds.
    // Deep scoring should recurse into the variant field's alternatives.
    struct HasIntOrString {
        std::variant<int, std::string> data;
    };

    struct HasBoolOrDouble {
        std::variant<bool, double> data;
    };

    using V = std::variant<HasIntOrString, HasBoolOrDouble>;

    V out{};
    ASSERT_TRUE(from_json(R"({"data":42})", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<int>(std::get<HasIntOrString>(out).data), 42);

    ASSERT_TRUE(from_json(R"({"data":true})", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<bool>(std::get<HasBoolOrDouble>(out).data), true);

    ASSERT_TRUE(from_json(R"({"data":"text"})", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<std::string>(std::get<HasIntOrString>(out).data), "text");

    ASSERT_TRUE(from_json(R"({"data":3.14})", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<double>(std::get<HasBoolOrDouble>(out).data), 3.14);
}

TEST_CASE(vector_vs_tuple_by_length) {
    // Tuple requires exact element count; vector accepts any length.
    // Deep scoring uses element count to disambiguate.
    using V = std::variant<std::tuple<int, std::string>, std::vector<int>>;

    V out{};
    ASSERT_TRUE(from_json(R"([1,2,3])", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<std::vector<int>>(out), std::vector<int>({1, 2, 3}));

    ASSERT_TRUE(from_json(R"([42,"hello"])", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    auto& [i, s] = std::get<std::tuple<int, std::string>>(out);
    EXPECT_EQ(i, 42);
    EXPECT_EQ(s, "hello");
}

TEST_CASE(triple_nested_variant) {
    // Three levels of variant nesting.
    using L0 = std::variant<bool, std::string>;
    using L1 = std::variant<L0, int>;
    using L2 = std::variant<L1, double>;

    L2 out{};

    // bool → L0 has bool (exact), L1 recurses to L0, L2 recurses to L1
    ASSERT_TRUE(from_json("true", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    auto& l1 = std::get<L1>(out);
    EXPECT_EQ(l1.index(), 0U);
    auto& l0 = std::get<L0>(l1);
    EXPECT_EQ(l0.index(), 0U);
    EXPECT_EQ(std::get<bool>(l0), true);

    // string → only L0 has string
    ASSERT_TRUE(from_json(R"("abc")", out).has_value());
    auto& l1s = std::get<L1>(out);
    auto& l0s = std::get<L0>(l1s);
    EXPECT_EQ(std::get<std::string>(l0s), "abc");

    // integer → L1 has int (exact match, quality 3), double has widening (quality 1)
    ASSERT_TRUE(from_json("99", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    auto& l1i = std::get<L1>(out);
    EXPECT_EQ(l1i.index(), 1U);
    EXPECT_EQ(std::get<int>(l1i), 99);

    // floating → only double accepts float
    ASSERT_TRUE(from_json("2.5", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<double>(out), 2.5);
}

TEST_CASE(variant_of_containers) {
    // Variant choosing between different container types.
    using V = std::variant<std::vector<int>, std::map<std::string, int>>;

    V out{};
    ASSERT_TRUE(from_json("[10,20]", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<std::vector<int>>(out), std::vector<int>({10, 20}));

    ASSERT_TRUE(from_json(R"({"a":1})", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<std::map<std::string, int>>(out).at("a"), 1);
}

TEST_CASE(struct_vs_map_object_scoring) {
    // Both struct and map accept object source; deep scoring differentiates.
    using V = std::variant<Point, std::map<std::string, double>>;

    V out{};
    // Point fields "x","y" match → struct scores higher than map
    ASSERT_TRUE(from_json(R"({"x":1.0,"y":2.0})", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<Point>(out), (Point{1.0, 2.0}));

    // Unknown fields → no struct field match, map still accepts
    ASSERT_TRUE(from_json(R"({"foo":3.0})", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<std::map<std::string, double>>(out).at("foo"), 3.0);
}

TEST_CASE(optional_wrapping_variant) {
    // optional<variant> and variant side by side
    using Inner = std::variant<int, std::string>;
    using V = std::variant<std::optional<Inner>, bool>;

    V out{};
    // null → optional accepts null
    ASSERT_TRUE(from_json("null", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_FALSE(std::get<std::optional<Inner>>(out).has_value());

    // bool → only bool alternative matches
    ASSERT_TRUE(from_json("true", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<bool>(out), true);

    // integer → optional<variant<int,string>> recurses and finds int
    ASSERT_TRUE(from_json("42", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    auto& opt = std::get<std::optional<Inner>>(out);
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(std::get<int>(*opt), 42);
}

TEST_CASE(shared_ptr_wrapping_variant) {
    using Inner = std::variant<int, std::string>;
    using V = std::variant<std::shared_ptr<Inner>, bool>;

    V out{};
    // null → shared_ptr accepts null
    ASSERT_TRUE(from_json("null", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<std::shared_ptr<Inner>>(out), nullptr);

    // string → recurse through shared_ptr to variant
    ASSERT_TRUE(from_json(R"("hello")", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    auto ptr = std::get<std::shared_ptr<Inner>>(out);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(std::get<std::string>(*ptr), "hello");
}

TEST_CASE(int_width_ordering) {
    // Wider integer wins when source is wider.
    using V = std::variant<std::int8_t, std::int16_t, std::int32_t, std::int64_t>;

    V out{};
    ASSERT_TRUE(from_json("42", out).has_value());
    // simdjson reports int64 for all integers → int64_t is exact match
    EXPECT_EQ(out.index(), 3U);
    EXPECT_EQ(std::get<std::int64_t>(out), 42);
}

TEST_CASE(mixed_numeric_precision) {
    // float32 vs float64 with floating source
    using V = std::variant<float, double>;

    V out{};
    ASSERT_TRUE(from_json("1.5", out).has_value());
    // simdjson reports float64 → double is exact match
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<double>(out), 1.5);
}

TEST_CASE(variant_roundtrip_complex) {
    // Roundtrip through serialization preserves the correct alternative.
    using V = std::
        variant<std::monostate, bool, std::int64_t, double, std::string, std::vector<int>, Point>;

    auto roundtrip = [](V input) -> bool {
        auto encoded = to_json(input);
        if(!encoded)
            return false;
        V out{};
        auto status = from_json(*encoded, out);
        if(!status)
            return false;
        return out.index() == input.index();
    };

    EXPECT_TRUE(roundtrip(std::monostate{}));
    EXPECT_TRUE(roundtrip(true));
    EXPECT_TRUE(roundtrip(std::int64_t{42}));
    EXPECT_TRUE(roundtrip(3.14));
    EXPECT_TRUE(roundtrip(std::string("test")));
    EXPECT_TRUE(roundtrip(std::vector<int>{1, 2, 3}));
    EXPECT_TRUE(roundtrip(Point{1.0, 2.0}));
}

TEST_CASE(two_structs_different_field_count) {
    // Struct with more matching fields scores higher.
    using V = std::variant<Color, Point>;

    V out{};
    // {"r":1,"g":2,"b":3} — all 3 fields match Color, no fields match Point
    ASSERT_TRUE(from_json(R"({"r":1,"g":2,"b":3})", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<Color>(out), (Color{1, 2, 3}));

    // {"x":1.0,"y":2.0} — both fields match Point, no fields match Color
    ASSERT_TRUE(from_json(R"({"x":1.0,"y":2.0})", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<Point>(out), (Point{1.0, 2.0}));
}

TEST_CASE(empty_object_scoring) {
    using V = std::variant<Point, std::map<std::string, int>>;

    V out{};
    // Empty object has no matching struct fields → map wins
    ASSERT_TRUE(from_json(R"({})", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_TRUE(std::get<std::map<std::string, int>>(out).empty());
}

TEST_CASE(empty_array_scoring) {
    using V = std::variant<std::vector<int>, std::string>;

    V out{};
    // Empty array → vector accepts it
    ASSERT_TRUE(from_json(R"([])", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_TRUE(std::get<std::vector<int>>(out).empty());
}

TEST_CASE(same_field_same_type_struct_tie) {
    // Two structs with identical field names and types — first wins
    struct A {
        int value;
    };

    struct B {
        int value;
    };

    using V = std::variant<A, B>;

    V out{};
    ASSERT_TRUE(from_json(R"({"value":1})", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<A>(out).value, 1);
}

TEST_CASE(variant_wrapper_no_inflation) {
    // variant<variant<double, int>, int> facing integer source:
    // The nested variant should NOT score higher than the direct int.
    using Inner = std::variant<double, int>;
    using V = std::variant<Inner, int>;

    V out{};
    ASSERT_TRUE(from_json("42", out).has_value());
    // Both alternatives accept int. The nested variant's best inner match (int)
    // should score the same as the direct int. First one wins on tie.
    EXPECT_EQ(out.index(), 0U);
    auto& inner = std::get<Inner>(out);
    EXPECT_EQ(inner.index(), 1U);
    EXPECT_EQ(std::get<int>(inner), 42);
}

TEST_CASE(field_subset_superset_matching) {
    // Struct with more matching fields should score higher.
    // {a, b} matches StructAB (2 fields) better than StructA (1 field).
    struct StructA {
        int a;
    };

    struct StructAB {
        int a;
        int b;
    };

    using V = std::variant<StructA, StructAB>;

    V out{};
    ASSERT_TRUE(from_json(R"({"a":1,"b":2})", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<StructAB>(out).a, 1);
    EXPECT_EQ(std::get<StructAB>(out).b, 2);

    // {a} alone — both accept, but StructA matches exactly
    ASSERT_TRUE(from_json(R"({"a":1})", out).has_value());
    // StructA has 1 field match, StructAB also has 1 field match → tie, first wins
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<StructA>(out).a, 1);
}

TEST_CASE(recursive_map_scoring) {
    using V = std::variant<std::map<std::string, int>, std::map<std::string, std::string>>;

    V out{};
    ASSERT_TRUE(from_json(R"({"a":1,"b":2})", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<std::map<std::string, int>>(out).at("a"), 1);

    ASSERT_TRUE(from_json(R"({"a":"x","b":"y"})", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<std::map<std::string, std::string>>(out).at("a"), "x");
}

TEST_CASE(subset_with_required_field_missing) {
    // Two structs sharing a common field, but the larger struct has a required
    // field not present in the input.  The smaller struct (whose required fields
    // are all satisfied) should win.
    struct Partial {
        int id;
        std::string value;
    };

    struct Whole {
        std::string value;
    };

    using V = std::variant<Partial, Whole>;

    V out{};
    // Only "value" present — Partial.id is required and missing → Whole should win
    ASSERT_TRUE(from_json(R"({"value":"hello"})", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<Whole>(out).value, "hello");

    // Both "id" and "value" present — Partial matches more fields → Partial wins
    ASSERT_TRUE(from_json(R"({"id":42,"value":"hello"})", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<Partial>(out).id, 42);
    EXPECT_EQ(std::get<Partial>(out).value, "hello");
}

TEST_CASE(subset_with_optional_field_missing) {
    // When the missing field has skip_if (e.g. skip_if_none), it should NOT be penalized.
    struct WithOptional {
        std::string value;
        skip_if_none<int> extra;
    };

    struct Plain {
        std::string value;
    };

    using V = std::variant<WithOptional, Plain>;

    V out{};
    // "extra" is optional (has default) → WithOptional still viable, first wins on tie
    ASSERT_TRUE(from_json(R"({"value":"hello"})", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<WithOptional>(out).value, "hello");
}

};  // TEST_SUITE(serde_variant_deep_dispatch)

}  // namespace

}  // namespace kota::codec
