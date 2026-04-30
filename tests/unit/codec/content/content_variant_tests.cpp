#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "fixtures/schema/common.h"
#include "fixtures/schema/tagged.h"
#include "kota/zest/zest.h"
#include "kota/codec/content/content.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

namespace {

using Point = meta::fixtures::Point2d;
using IntHolder = meta::fixtures::IntHolder;
using StringHolder = meta::fixtures::StringHolder;
using Circle = meta::fixtures::Circle;
using Rect = meta::fixtures::Rect;

using IntTagShape = meta::annotation<std::variant<Circle, Rect>,
                                    meta::attrs::internally_tagged<"type">::names<"circle", "rect">>;

template <typename T>
auto from_content(const content::Value& val, T& out) -> std::expected<void, content::error> {
    content::Deserializer<> d(val);
    KOTA_EXPECTED_TRY(codec::deserialize(d, out));
    return d.finish();
}

TEST_SUITE(serde_content_variant) {

TEST_CASE(int_vs_string) {
    using V = std::variant<int, std::string>;

    V out{};
    ASSERT_TRUE(from_content(content::Value(std::int64_t{42}), out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<int>(out), 42);

    ASSERT_TRUE(from_content(content::Value("hello"), out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<std::string>(out), "hello");
}

TEST_CASE(bool_vs_int) {
    using V = std::variant<bool, int>;

    V out{};
    ASSERT_TRUE(from_content(content::Value(true), out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<bool>(out), true);

    ASSERT_TRUE(from_content(content::Value(std::int64_t{7}), out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<int>(out), 7);
}

TEST_CASE(int_before_double) {
    using V = std::variant<int, double>;

    V out{};
    ASSERT_TRUE(from_content(content::Value(std::int64_t{42}), out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<int>(out), 42);

    ASSERT_TRUE(from_content(content::Value(3.14), out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<double>(out), 3.14);
}

TEST_CASE(int64_vs_uint64) {
    using V = std::variant<std::int64_t, std::uint64_t>;

    V out{};
    ASSERT_TRUE(from_content(content::Value(std::int64_t{42}), out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<std::int64_t>(out), 42);

    ASSERT_TRUE(from_content(content::Value(std::uint64_t{UINT64_MAX}), out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<std::uint64_t>(out), UINT64_MAX);
}

TEST_CASE(monostate_matches_null) {
    using V = std::variant<std::monostate, int>;

    V out = 42;
    ASSERT_TRUE(from_content(content::Value(nullptr), out).has_value());
    EXPECT_EQ(out.index(), 0U);
}

TEST_CASE(struct_deep_scoring) {
    using V = std::variant<IntHolder, StringHolder>;

    content::Object obj_int;
    obj_int.insert("value", content::Value(std::int64_t{42}));

    V out{};
    ASSERT_TRUE(from_content(content::Value(std::move(obj_int)), out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<IntHolder>(out).value, 42);

    content::Object obj_str;
    obj_str.insert("value", content::Value("hello"));

    ASSERT_TRUE(from_content(content::Value(std::move(obj_str)), out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<StringHolder>(out).value, "hello");
}

TEST_CASE(array_vs_object) {
    using V = std::variant<std::vector<int>, std::map<std::string, int>>;

    content::Array arr;
    arr.push_back(content::Value(std::int64_t{1}));
    arr.push_back(content::Value(std::int64_t{2}));

    V out{};
    ASSERT_TRUE(from_content(content::Value(std::move(arr)), out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<std::vector<int>>(out), std::vector<int>({1, 2}));

    content::Object obj;
    obj.insert("a", content::Value(std::int64_t{1}));
    obj.insert("b", content::Value(std::int64_t{2}));

    ASSERT_TRUE(from_content(content::Value(std::move(obj)), out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<std::map<std::string, int>>(out).at("a"), 1);
}

TEST_CASE(struct_vs_map_scoring) {
    using V = std::variant<Point, std::map<std::string, double>>;

    content::Object obj_point;
    obj_point.insert("x", content::Value(1.0));
    obj_point.insert("y", content::Value(2.0));

    V out{};
    ASSERT_TRUE(from_content(content::Value(std::move(obj_point)), out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<Point>(out), (Point{1.0, 2.0}));

    content::Object obj_map;
    obj_map.insert("foo", content::Value(3.0));

    ASSERT_TRUE(from_content(content::Value(std::move(obj_map)), out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<std::map<std::string, double>>(out).at("foo"), 3.0);
}

TEST_CASE(no_match_fails) {
    using V = std::variant<int, std::string>;

    V out{};
    EXPECT_FALSE(from_content(content::Value(true), out).has_value());

    content::Array arr;
    EXPECT_FALSE(from_content(content::Value(std::move(arr)), out).has_value());
}

TEST_CASE(internally_tagged) {
    struct Holder {
        IntTagShape shape;
    };

    content::Object shape_obj;
    shape_obj.insert("type", content::Value("circle"));
    shape_obj.insert("radius", content::Value(5.0));

    content::Object root;
    root.insert("shape", content::Value(std::move(shape_obj)));

    Holder out{};
    ASSERT_TRUE(from_content(content::Value(std::move(root)), out).has_value());
    EXPECT_EQ(std::get<Circle>(out.shape).radius, 5.0);
}

TEST_CASE(empty_object_scoring) {
    using V = std::variant<Point, std::map<std::string, int>>;

    content::Object empty;

    V out{};
    ASSERT_TRUE(from_content(content::Value(std::move(empty)), out).has_value());
    EXPECT_EQ(out.index(), 1U);
}

TEST_CASE(empty_array_scoring) {
    using V = std::variant<std::vector<int>, std::string>;

    content::Array empty;

    V out{};
    ASSERT_TRUE(from_content(content::Value(std::move(empty)), out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_TRUE(std::get<std::vector<int>>(out).empty());
}

TEST_CASE(json_to_content_variant_roundtrip) {
    using V = std::variant<int, std::string>;

    auto parsed = json::from_json<content::Value>(R"(42)");
    ASSERT_TRUE(parsed.has_value());

    V out{};
    ASSERT_TRUE(from_content(*parsed, out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<int>(out), 42);
}

};  // TEST_SUITE(serde_content_variant)

TEST_SUITE(serde_content_peek_kind_trait) {

TEST_CASE(json_to_content_scalars) {
    auto test = [](std::string_view json_str, content::ValueKind expected_kind) -> bool {
        auto parsed = json::from_json<content::Value>(json_str);
        if(!parsed.has_value())
            return false;
        return parsed->kind() == expected_kind;
    };

    EXPECT_TRUE(test("null", content::ValueKind::null_value));
    EXPECT_TRUE(test("true", content::ValueKind::boolean));
    EXPECT_TRUE(test("42", content::ValueKind::signed_int));
    EXPECT_TRUE(test("18446744073709551615", content::ValueKind::unsigned_int));
    EXPECT_TRUE(test("3.14", content::ValueKind::floating));
    EXPECT_TRUE(test(R"("hello")", content::ValueKind::string));
}

TEST_CASE(json_to_content_array) {
    auto parsed = json::from_json<content::Value>(R"([1,2,3])");
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->is_array());
    auto* arr = parsed->get_array();
    ASSERT_NE(arr, nullptr);
    ASSERT_EQ(arr->size(), 3U);
    EXPECT_EQ((*arr)[0].as_int(), 1);
    EXPECT_EQ((*arr)[1].as_int(), 2);
    EXPECT_EQ((*arr)[2].as_int(), 3);
}

TEST_CASE(json_to_content_object) {
    auto parsed = json::from_json<content::Value>(R"({"a":1,"b":"two"})");
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->is_object());
    EXPECT_EQ((*parsed)["a"].as_int(), 1);
    EXPECT_EQ((*parsed)["b"].as_string(), "two");
}

TEST_CASE(json_to_content_nested) {
    auto parsed = json::from_json<content::Value>(R"({"items":[{"x":1},{"x":2}]})");
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->is_object());
    auto items = (*parsed)["items"];
    ASSERT_TRUE(items.valid());
    EXPECT_EQ(items[0]["x"].as_int(), 1);
    EXPECT_EQ(items[1]["x"].as_int(), 2);
}

TEST_CASE(json_to_content_struct) {
    auto parsed = json::from_json<content::Value>(R"({"x":1.5,"y":2.5})");
    ASSERT_TRUE(parsed.has_value());

    Point point{};
    ASSERT_TRUE(from_content(*parsed, point).has_value());
    EXPECT_EQ(point, (Point{1.5, 2.5}));
}

TEST_CASE(json_to_content_complex_roundtrip) {
    auto dom = json::from_json<content::Value>(
        R"({"name":"test","scores":[1,2,3],"nested":{"flag":true}})");
    ASSERT_TRUE(dom.has_value());
    ASSERT_TRUE(dom->is_object());
    EXPECT_EQ((*dom)["name"].as_string(), "test");
    EXPECT_EQ((*dom)["scores"][0].as_int(), 1);
    EXPECT_EQ((*dom)["nested"]["flag"].as_bool(), true);
}

};  // TEST_SUITE(serde_content_peek_kind_trait)

}  // namespace

}  // namespace kota::codec
