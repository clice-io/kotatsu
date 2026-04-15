#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "eventide/zest/zest.h"
#include "eventide/serde/json/deserializer.h"
#include "eventide/serde/json/serializer.h"
#include "eventide/serde/serde/serde.h"

namespace eventide::serde {

using namespace refl;

namespace {

using json::from_json;
using json::to_json;

// ── Helper types ─────────────────────────────────────────────────────

struct Point {
    double x{};
    double y{};

    auto operator==(const Point&) const -> bool = default;
};

struct Color {
    std::int32_t r{};
    std::int32_t g{};
    std::int32_t b{};

    auto operator==(const Color&) const -> bool = default;
};

struct IntHolder {
    std::int32_t value{};

    auto operator==(const IntHolder&) const -> bool = default;
};

struct StringHolder {
    std::string value;

    auto operator==(const StringHolder&) const -> bool = default;
};

struct Empty {
    auto operator==(const Empty&) const -> bool = default;
};

// ── Tagged variant type aliases ──────────────────────────────────────

// External
using ExtSimple =
    annotation<std::variant<int, std::string>, refl::attrs::externally_tagged::names<"num", "str">>;

using ExtWithMono = annotation<std::variant<std::monostate, int, std::string>,
                               refl::attrs::externally_tagged::names<"none", "num", "str">>;

using ExtWithStruct = annotation<std::variant<int, Point, Color>,
                                 refl::attrs::externally_tagged::names<"int", "point", "color">>;

// Adjacent
using AdjSimple = annotation<std::variant<int, std::string>,
                             refl::attrs::adjacently_tagged<"t", "v">::names<"num", "str">>;

using AdjWithMono =
    annotation<std::variant<std::monostate, int, std::string>,
               refl::attrs::adjacently_tagged<"tag", "data">::names<"nil", "num", "str">>;

using AdjWithStruct =
    annotation<std::variant<int, Point>,
               refl::attrs::adjacently_tagged<"type", "value">::names<"int", "point">>;

// Internal
struct Circle {
    double radius{};

    auto operator==(const Circle&) const -> bool = default;
};

struct Rect {
    double width{};
    double height{};

    auto operator==(const Rect&) const -> bool = default;
};

struct Triangle {
    double base{};
    double height{};

    auto operator==(const Triangle&) const -> bool = default;
};

using IntTagShape = annotation<std::variant<Circle, Rect>,
                               refl::attrs::internally_tagged<"type">::names<"circle", "rect">>;

using IntTagTriShape =
    annotation<std::variant<Circle, Rect, Triangle>,
               refl::attrs::internally_tagged<"kind">::names<"circle", "rect", "triangle">>;

// ── Containers with tagged variants ──────────────────────────────────

struct ExtHolder {
    std::string label;
    ExtWithStruct item;

    auto operator==(const ExtHolder&) const -> bool = default;
};

struct AdjHolder {
    std::string name;
    AdjSimple data;

    auto operator==(const AdjHolder&) const -> bool = default;
};

struct IntTagHolder {
    std::string name;
    IntTagShape shape;

    auto operator==(const IntTagHolder&) const -> bool = default;
};

// ═══════════════════════════════════════════════════════════════════════
// §1  Untagged variant — type-hint discrimination
// ═══════════════════════════════════════════════════════════════════════

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
    // When double comes first, integer JSON matches double (it accepts integer | floating)
    using V = std::variant<double, int>;

    V out{};
    ASSERT_TRUE(from_json("42", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<double>(out), 42.0);

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

TEST_CASE(struct_backtracking) {
    // Two struct types that are both objects; the first one should fail and
    // the second one should succeed
    using V = std::variant<IntHolder, StringHolder>;

    V out{};
    ASSERT_TRUE(from_json(R"({"value":"hello"})", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<StringHolder>(out).value, "hello");

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

};  // TEST_SUITE(serde_variant_untagged)

// ═══════════════════════════════════════════════════════════════════════
// §2  Externally tagged — detailed tests
// ═══════════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════════
// §3  Adjacently tagged — detailed tests
// ═══════════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════════
// §4  Internally tagged — detailed tests
// ═══════════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════════
// §5  Nested variants and complex compositions
// ═══════════════════════════════════════════════════════════════════════

TEST_SUITE(serde_variant_nested) {

TEST_CASE(variant_in_struct_in_variant) {
    // An externally tagged variant whose struct alternative contains another ext variant
    using Inner =
        annotation<std::variant<int, std::string>, refl::attrs::externally_tagged::names<"i", "s">>;

    struct Wrapper {
        std::string id;
        Inner val;

        auto operator==(const Wrapper&) const -> bool = default;
    };

    using Outer = annotation<std::variant<int, Wrapper>,
                             refl::attrs::externally_tagged::names<"plain", "wrapped">>;

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
                         refl::attrs::adjacently_tagged<"t", "v">::names<"i", "s">>;

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

}  // namespace

}  // namespace eventide::serde
