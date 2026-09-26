#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "fixtures/schema/common.h"
#include "fixtures/schema/tagged.h"
#include "kota/zest/zest.h"
#include "kota/codec/dyn/dyn.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

namespace {

using Point = meta::fixtures::Point2d;
using IntHolder = meta::fixtures::IntHolder;
using StringHolder = meta::fixtures::StringHolder;
using Circle = meta::fixtures::Circle;
using Rect = meta::fixtures::Rect;

KOTATSU_ANNOTATION(int_tag_shape_annotation, tag = "type", tag_names = {"circle", "rect"});
using IntTagShape = meta::annotate<int_tag_shape_annotation>::type<std::variant<Circle, Rect>>;

ZEST_SUITE(serde_dyn_variant){

    ZEST_CASE(int_vs_string){using V = std::variant<int, std::string>;

V out{};
ASSERT(dyn::from_dyn(dyn::Value(std::int64_t{42}), out).has_value());
EXPECT(out.index() == 0U);
EXPECT(std::get<int>(out) == 42);

ASSERT(dyn::from_dyn(dyn::Value("hello"), out).has_value());
EXPECT(out.index() == 1U);
EXPECT(std::get<std::string>(out) == "hello");

}  // namespace

ZEST_CASE(bool_vs_int) {
    using V = std::variant<bool, int>;

    V out{};
    ASSERT(dyn::from_dyn(dyn::Value(true), out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<bool>(out) == true);

    ASSERT(dyn::from_dyn(dyn::Value(std::int64_t{7}), out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<int>(out) == 7);
}

ZEST_CASE(int_before_double) {
    using V = std::variant<int, double>;

    V out{};
    ASSERT(dyn::from_dyn(dyn::Value(std::int64_t{42}), out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<int>(out) == 42);

    ASSERT(dyn::from_dyn(dyn::Value(3.14), out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<double>(out) == 3.14);
}

ZEST_CASE(int64_vs_uint64) {
    using V = std::variant<std::int64_t, std::uint64_t>;

    V out{};
    ASSERT(dyn::from_dyn(dyn::Value(std::int64_t{42}), out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::int64_t>(out) == 42);

    ASSERT(dyn::from_dyn(dyn::Value(std::uint64_t{UINT64_MAX}), out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::uint64_t>(out) == UINT64_MAX);
}

ZEST_CASE(monostate_matches_null) {
    using V = std::variant<std::monostate, int>;

    V out = 42;
    ASSERT(dyn::from_dyn(dyn::Value(nullptr), out).has_value());
    EXPECT(out.index() == 0U);
}

ZEST_CASE(struct_deep_scoring) {
    using V = std::variant<IntHolder, StringHolder>;

    dyn::Object obj_int;
    obj_int.insert("value", dyn::Value(std::int64_t{42}));

    V out{};
    ASSERT(dyn::from_dyn(dyn::Value(std::move(obj_int)), out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<IntHolder>(out).value == 42);

    dyn::Object obj_str;
    obj_str.insert("value", dyn::Value("hello"));

    ASSERT(dyn::from_dyn(dyn::Value(std::move(obj_str)), out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<StringHolder>(out).value == "hello");
}

ZEST_CASE(array_vs_object) {
    using V = std::variant<std::vector<int>, std::map<std::string, int>>;

    dyn::Array arr;
    arr.push_back(dyn::Value(std::int64_t{1}));
    arr.push_back(dyn::Value(std::int64_t{2}));

    V out{};
    ASSERT(dyn::from_dyn(dyn::Value(std::move(arr)), out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::vector<int>>(out) == std::vector<int>({1, 2}));

    dyn::Object obj;
    obj.insert("a", dyn::Value(std::int64_t{1}));
    obj.insert("b", dyn::Value(std::int64_t{2}));

    ASSERT(dyn::from_dyn(dyn::Value(std::move(obj)), out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::map<std::string, int>>(out).at("a") == 1);
}

ZEST_CASE(struct_vs_map_scoring) {
    using V = std::variant<Point, std::map<std::string, double>>;

    dyn::Object obj_point;
    obj_point.insert("x", dyn::Value(1.0));
    obj_point.insert("y", dyn::Value(2.0));

    V out{};
    ASSERT(dyn::from_dyn(dyn::Value(std::move(obj_point)), out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<Point>(out) == (Point{1.0, 2.0}));

    dyn::Object obj_map;
    obj_map.insert("foo", dyn::Value(3.0));

    ASSERT(dyn::from_dyn(dyn::Value(std::move(obj_map)), out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::map<std::string, double>>(out).at("foo") == 3.0);
}

ZEST_CASE(no_match_fails) {
    using V = std::variant<int, std::string>;

    V out{};
    EXPECT(!dyn::from_dyn(dyn::Value(true), out).has_value());

    dyn::Array arr;
    EXPECT(!dyn::from_dyn(dyn::Value(std::move(arr)), out).has_value());
}

ZEST_CASE(internally_tagged) {
    struct Holder {
        IntTagShape shape;
    };

    dyn::Object shape_obj;
    shape_obj.insert("type", dyn::Value("circle"));
    shape_obj.insert("radius", dyn::Value(5.0));

    dyn::Object root;
    root.insert("shape", dyn::Value(std::move(shape_obj)));

    Holder out{};
    ASSERT(dyn::from_dyn(dyn::Value(std::move(root)), out).has_value());
    EXPECT(std::get<Circle>(out.shape).radius == 5.0);
}

ZEST_CASE(empty_object_scoring) {
    using V = std::variant<Point, std::map<std::string, int>>;

    dyn::Object empty;

    V out{};
    ASSERT(dyn::from_dyn(dyn::Value(std::move(empty)), out).has_value());
    EXPECT(out.index() == 1U);
}

ZEST_CASE(empty_array_scoring) {
    using V = std::variant<std::vector<int>, std::string>;

    dyn::Array empty;

    V out{};
    ASSERT(dyn::from_dyn(dyn::Value(std::move(empty)), out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::vector<int>>(out).empty());
}

ZEST_CASE(json_to_dyn_variant_roundtrip) {
    using V = std::variant<int, std::string>;

    auto parsed = json::from_string<dyn::Value>(R"(42)");
    ASSERT(parsed.has_value());

    V out{};
    ASSERT(dyn::from_dyn(*parsed, out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<int>(out) == 42);
}

};  // namespace kota::codec

ZEST_SUITE(serde_dyn_peek_kind_trait){

    ZEST_CASE(json_to_dyn_scalars){
        auto test = [](std::string_view json_str, dyn::ValueKind expected_kind) -> bool {
            auto parsed = json::from_string<dyn::Value>(json_str);
            if(!parsed.has_value())
                return false;
            return parsed->kind() == expected_kind;
        };

EXPECT(test("null", dyn::ValueKind::null_value));
EXPECT(test("true", dyn::ValueKind::boolean));
EXPECT(test("42", dyn::ValueKind::signed_int));
EXPECT(test("18446744073709551615", dyn::ValueKind::unsigned_int));
EXPECT(test("3.14", dyn::ValueKind::floating));
EXPECT(test(R"("hello")", dyn::ValueKind::string));
}

ZEST_CASE(json_to_dyn_array) {
    auto parsed = json::from_string<dyn::Value>(R"([1,2,3])");
    ASSERT(parsed.has_value());
    ASSERT(parsed->is_array());
    auto* arr = parsed->get_array();
    ASSERT(arr != nullptr);
    ASSERT(arr->size() == 3U);
    EXPECT((*arr)[0].as_int() == 1);
    EXPECT((*arr)[1].as_int() == 2);
    EXPECT((*arr)[2].as_int() == 3);
}

ZEST_CASE(json_to_dyn_object) {
    auto parsed = json::from_string<dyn::Value>(R"({"a":1,"b":"two"})");
    ASSERT(parsed.has_value());
    ASSERT(parsed->is_object());
    EXPECT((*parsed)["a"].as_int() == 1);
    EXPECT((*parsed)["b"].as_string() == "two");
}

ZEST_CASE(json_to_dyn_nested) {
    auto parsed = json::from_string<dyn::Value>(R"({"items":[{"x":1},{"x":2}]})");
    ASSERT(parsed.has_value());
    ASSERT(parsed->is_object());
    auto items = (*parsed)["items"];
    ASSERT(items.valid());
    EXPECT(items[0]["x"].as_int() == 1);
    EXPECT(items[1]["x"].as_int() == 2);
}

ZEST_CASE(json_to_dyn_struct) {
    auto parsed = json::from_string<dyn::Value>(R"({"x":1.5,"y":2.5})");
    ASSERT(parsed.has_value());

    Point point{};
    ASSERT(dyn::from_dyn(*parsed, point).has_value());
    EXPECT(point == (Point{1.5, 2.5}));
}

ZEST_CASE(json_to_dyn_complex_roundtrip) {
    auto dom =
        json::from_string<dyn::Value>(R"({"name":"test","scores":[1,2,3],"nested":{"flag":true}})");
    ASSERT(dom.has_value());
    ASSERT(dom->is_object());
    EXPECT((*dom)["name"].as_string() == "test");
    EXPECT((*dom)["scores"][0].as_int() == 1);
    EXPECT((*dom)["nested"]["flag"].as_bool() == true);
}
}
;  // ZEST_SUITE(serde_dyn_peek_kind_trait)

}  // namespace

}  // namespace kota::codec
