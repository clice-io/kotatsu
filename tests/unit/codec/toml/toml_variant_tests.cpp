#if __has_include(<toml++/toml.hpp>)

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "fixtures/schema/common.h"
#include "fixtures/schema/tagged.h"
#include "kota/zest/zest.h"
#include "kota/codec/toml/toml.h"

namespace kota::codec {

namespace {

using toml::from_toml;
using toml::parse;
using toml::to_toml;

using Point = meta::fixtures::Point2d;
using IntHolder = meta::fixtures::IntHolder;
using StringHolder = meta::fixtures::StringHolder;

using Circle = meta::fixtures::Circle;
using Rect = meta::fixtures::Rect;

using IntTagShape =
    meta::annotation<std::variant<Circle, Rect>,
                     meta::attrs::internally_tagged<"type">::names<"circle", "rect">>;

using ExtTagShape = meta::annotation<std::variant<int, std::string>,
                                     meta::attrs::externally_tagged::names<"integer", "text">>;

using AdjTagShape =
    meta::annotation<std::variant<int, std::string>,
                     meta::attrs::adjacently_tagged<"type", "value">::names<"integer", "text">>;

struct VariantField {
    std::variant<int, std::string> value;
};

struct NestedVariantField {
    std::variant<int, double> num;
    std::string label;
};

TEST_SUITE(serde_toml_variant_untagged) {

TEST_CASE(int_vs_string) {
    using V = std::variant<int, std::string>;

    struct Holder {
        V data;
    };

    auto tbl_int = ::toml::table{
        {"data", 42}
    };
    Holder out{};
    ASSERT_TRUE(from_toml(tbl_int, out).has_value());
    EXPECT_EQ(out.data.index(), 0U);
    EXPECT_EQ(std::get<int>(out.data), 42);

    auto tbl_str = ::toml::table{
        {"data", "hello"}
    };
    ASSERT_TRUE(from_toml(tbl_str, out).has_value());
    EXPECT_EQ(out.data.index(), 1U);
    EXPECT_EQ(std::get<std::string>(out.data), "hello");
}

TEST_CASE(int_before_double) {
    using V = std::variant<int, double>;

    struct Holder {
        V num;
    };

    auto tbl = ::toml::table{
        {"num", 42}
    };
    Holder out{};
    ASSERT_TRUE(from_toml(tbl, out).has_value());
    EXPECT_EQ(out.num.index(), 0U);
    EXPECT_EQ(std::get<int>(out.num), 42);

    auto tbl_f = ::toml::table{
        {"num", 3.14}
    };
    ASSERT_TRUE(from_toml(tbl_f, out).has_value());
    EXPECT_EQ(out.num.index(), 1U);
    EXPECT_EQ(std::get<double>(out.num), 3.14);
}

TEST_CASE(bool_vs_int) {
    using V = std::variant<bool, int>;

    struct Holder {
        V data;
    };

    auto tbl_bool = ::toml::table{
        {"data", true}
    };
    Holder out{};
    ASSERT_TRUE(from_toml(tbl_bool, out).has_value());
    EXPECT_EQ(out.data.index(), 0U);
    EXPECT_EQ(std::get<bool>(out.data), true);

    auto tbl_int = ::toml::table{
        {"data", 7}
    };
    ASSERT_TRUE(from_toml(tbl_int, out).has_value());
    EXPECT_EQ(out.data.index(), 1U);
    EXPECT_EQ(std::get<int>(out.data), 7);
}

TEST_CASE(struct_deep_scoring) {
    using V = std::variant<IntHolder, StringHolder>;

    struct Holder {
        V item;
    };

    auto tbl_int = ::toml::table{
        {"item", ::toml::table{{"value", 42}}}
    };
    Holder out{};
    ASSERT_TRUE(from_toml(tbl_int, out).has_value());
    EXPECT_EQ(out.item.index(), 0U);
    EXPECT_EQ(std::get<IntHolder>(out.item).value, 42);

    auto tbl_str = ::toml::table{
        {"item", ::toml::table{{"value", "hello"}}}
    };
    ASSERT_TRUE(from_toml(tbl_str, out).has_value());
    EXPECT_EQ(out.item.index(), 1U);
    EXPECT_EQ(std::get<StringHolder>(out.item).value, "hello");
}

TEST_CASE(array_vs_table) {
    using V = std::variant<std::vector<int>, std::map<std::string, int>>;

    struct Holder {
        V data;
    };

    ::toml::array arr;
    arr.push_back(1);
    arr.push_back(2);
    arr.push_back(3);
    auto tbl_arr = ::toml::table{
        {"data", std::move(arr)}
    };
    Holder out{};
    ASSERT_TRUE(from_toml(tbl_arr, out).has_value());
    EXPECT_EQ(out.data.index(), 0U);
    EXPECT_EQ(std::get<std::vector<int>>(out.data), std::vector<int>({1, 2, 3}));

    auto tbl_map = ::toml::table{
        {"data", ::toml::table{{"a", 1}, {"b", 2}}}
    };
    ASSERT_TRUE(from_toml(tbl_map, out).has_value());
    EXPECT_EQ(out.data.index(), 1U);
    auto& m = std::get<std::map<std::string, int>>(out.data);
    EXPECT_EQ(m.size(), 2U);
    EXPECT_EQ(m["a"], 1);
    EXPECT_EQ(m["b"], 2);
}

TEST_CASE(struct_vs_map_scoring) {
    using V = std::variant<Point, std::map<std::string, double>>;

    struct Holder {
        V data;
    };

    auto tbl_point = ::toml::table{
        {"data", ::toml::table{{"x", 1.0}, {"y", 2.0}}}
    };
    Holder out{};
    ASSERT_TRUE(from_toml(tbl_point, out).has_value());
    EXPECT_EQ(out.data.index(), 0U);
    EXPECT_EQ(std::get<Point>(out.data), (Point{1.0, 2.0}));

    auto tbl_map = ::toml::table{
        {"data", ::toml::table{{"foo", 3.0}}}
    };
    ASSERT_TRUE(from_toml(tbl_map, out).has_value());
    EXPECT_EQ(out.data.index(), 1U);
    EXPECT_EQ(std::get<std::map<std::string, double>>(out.data).at("foo"), 3.0);
}

TEST_CASE(no_match_fails) {
    using V = std::variant<int, std::string>;

    struct Holder {
        V data;
    };

    auto tbl_bool = ::toml::table{
        {"data", true}
    };
    Holder out{};
    EXPECT_FALSE(from_toml(tbl_bool, out).has_value());
}

TEST_CASE(roundtrip) {
    VariantField input{.value = 42};
    auto dom = to_toml(input);
    ASSERT_TRUE(dom.has_value());

    VariantField out{};
    ASSERT_TRUE(from_toml(*dom, out).has_value());
    EXPECT_EQ(out.value.index(), 0U);
    EXPECT_EQ(std::get<int>(out.value), 42);

    VariantField input2{.value = std::string("test")};
    dom = to_toml(input2);
    ASSERT_TRUE(dom.has_value());

    ASSERT_TRUE(from_toml(*dom, out).has_value());
    EXPECT_EQ(out.value.index(), 1U);
    EXPECT_EQ(std::get<std::string>(out.value), "test");
}

TEST_CASE(parse_text) {
    struct Holder {
        std::variant<int, std::string> val;
    };

    auto result = parse<Holder>("val = 99\n");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->val.index(), 0U);
    EXPECT_EQ(std::get<int>(result->val), 99);

    result = parse<Holder>(R"(val = "abc")");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->val.index(), 1U);
    EXPECT_EQ(std::get<std::string>(result->val), "abc");
}

TEST_CASE(empty_object_scoring) {
    using V = std::variant<Point, std::map<std::string, int>>;

    struct Holder {
        V data;
    };

    auto tbl_empty = ::toml::table{
        {"data", ::toml::table{}}
    };
    Holder out{};
    ASSERT_TRUE(from_toml(tbl_empty, out).has_value());
    EXPECT_EQ(out.data.index(), 1U);
}

TEST_CASE(empty_array_scoring) {
    using V = std::variant<std::vector<int>, std::string>;

    struct Holder {
        V data;
    };

    ::toml::array arr;
    auto tbl = ::toml::table{
        {"data", std::move(arr)}
    };
    Holder out{};
    ASSERT_TRUE(from_toml(tbl, out).has_value());
    EXPECT_EQ(out.data.index(), 0U);
    EXPECT_TRUE(std::get<std::vector<int>>(out.data).empty());
}

TEST_CASE(field_subset_match) {
    using V = std::variant<Point, Circle>;

    struct Holder {
        V shape;
    };

    auto tbl = ::toml::table{
        {"shape", ::toml::table{{"radius", 5.0}}}
    };
    Holder out{};
    ASSERT_TRUE(from_toml(tbl, out).has_value());
    EXPECT_EQ(out.shape.index(), 1U);
    EXPECT_EQ(std::get<Circle>(out.shape).radius, 5.0);

    auto tbl2 = ::toml::table{
        {"shape", ::toml::table{{"x", 1.0}, {"y", 2.0}}}
    };
    ASSERT_TRUE(from_toml(tbl2, out).has_value());
    EXPECT_EQ(out.shape.index(), 0U);
    EXPECT_EQ(std::get<Point>(out.shape), (Point{1.0, 2.0}));
}

};  // TEST_SUITE(serde_toml_variant_untagged)

TEST_SUITE(serde_toml_variant_internally_tagged) {

TEST_CASE(circle_roundtrip) {
    struct Holder {
        IntTagShape shape;
    };

    auto tbl = ::toml::table{
        {"shape", ::toml::table{{"type", "circle"}, {"radius", 5.0}}}
    };
    Holder out{};
    ASSERT_TRUE(from_toml(tbl, out).has_value());
    EXPECT_EQ(std::get<Circle>(out.shape), (Circle{.radius = 5.0}));
}

TEST_CASE(rect_roundtrip) {
    struct Holder {
        IntTagShape shape;
    };

    auto tbl = ::toml::table{
        {"shape", ::toml::table{{"type", "rect"}, {"width", 3.0}, {"height", 4.0}}}
    };
    Holder out{};
    ASSERT_TRUE(from_toml(tbl, out).has_value());
    EXPECT_EQ(std::get<Rect>(out.shape), (Rect{3.0, 4.0}));
}

TEST_CASE(unknown_tag_fails) {
    struct Holder {
        IntTagShape shape;
    };

    auto tbl = ::toml::table{
        {"shape", ::toml::table{{"type", "pentagon"}, {"sides", 5}}}
    };
    Holder out{};
    EXPECT_FALSE(from_toml(tbl, out).has_value());
}

TEST_CASE(missing_tag_fails) {
    struct Holder {
        IntTagShape shape;
    };

    auto tbl = ::toml::table{
        {"shape", ::toml::table{{"radius", 5.0}}}
    };
    Holder out{};
    EXPECT_FALSE(from_toml(tbl, out).has_value());
}

TEST_CASE(roundtrip_via_serialization) {
    struct Holder {
        IntTagShape shape;
    };

    Holder input{.shape = Circle{.radius = 7.0}};
    auto dom = to_toml(input);
    ASSERT_TRUE(dom.has_value());

    Holder out{};
    ASSERT_TRUE(from_toml(*dom, out).has_value());
    EXPECT_EQ(std::get<Circle>(out.shape).radius, 7.0);
}

TEST_CASE(vector_of_tagged) {
    struct Holder {
        std::vector<IntTagShape> shapes;
    };

    ::toml::array arr;
    arr.push_back(::toml::table{
        {"type",   "circle"},
        {"radius", 1.0     }
    });
    arr.push_back(::toml::table{
        {"type",   "rect"},
        {"width",  2.0   },
        {"height", 3.0   }
    });
    auto tbl = ::toml::table{
        {"shapes", std::move(arr)}
    };

    Holder out{};
    ASSERT_TRUE(from_toml(tbl, out).has_value());
    ASSERT_EQ(out.shapes.size(), 2U);
    EXPECT_EQ(std::get<Circle>(out.shapes[0]).radius, 1.0);
    EXPECT_EQ(std::get<Rect>(out.shapes[1]), (Rect{2.0, 3.0}));
}

TEST_CASE(parse_text_internally_tagged) {
    struct Holder {
        IntTagShape shape;
    };

    auto result = parse<Holder>(R"(
[shape]
type = "circle"
radius = 2.5
)");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<Circle>(result->shape).radius, 2.5);
}

};  // TEST_SUITE(serde_toml_variant_internally_tagged)

TEST_SUITE(serde_toml_variant_externally_tagged) {

TEST_CASE(int_roundtrip) {
    struct Holder {
        ExtTagShape data;
    };

    auto tbl = ::toml::table{
        {"data", ::toml::table{{"integer", 42}}}
    };
    Holder out{};
    ASSERT_TRUE(from_toml(tbl, out).has_value());
    EXPECT_EQ(std::get<int>(out.data), 42);

    Holder input{.data = 42};
    auto dom = to_toml(input);
    ASSERT_TRUE(dom.has_value());
    ASSERT_TRUE(from_toml(*dom, out).has_value());
    EXPECT_EQ(std::get<int>(out.data), 42);
}

TEST_CASE(string_roundtrip) {
    struct Holder {
        ExtTagShape data;
    };

    auto tbl = ::toml::table{
        {"data", ::toml::table{{"text", "hello"}}}
    };
    Holder out{};
    ASSERT_TRUE(from_toml(tbl, out).has_value());
    EXPECT_EQ(std::get<std::string>(out.data), "hello");
}

TEST_CASE(unknown_tag_fails) {
    struct Holder {
        ExtTagShape data;
    };

    auto tbl = ::toml::table{
        {"data", ::toml::table{{"unknown", 1}}}
    };
    Holder out{};
    EXPECT_FALSE(from_toml(tbl, out).has_value());
}

};  // TEST_SUITE(serde_toml_variant_externally_tagged)

TEST_SUITE(serde_toml_variant_adjacently_tagged) {

TEST_CASE(int_roundtrip) {
    struct Holder {
        AdjTagShape data;
    };

    auto tbl = ::toml::table{
        {"data", ::toml::table{{"type", "integer"}, {"value", 42}}}
    };
    Holder out{};
    ASSERT_TRUE(from_toml(tbl, out).has_value());
    EXPECT_EQ(std::get<int>(out.data), 42);

    Holder input{.data = 42};
    auto dom = to_toml(input);
    ASSERT_TRUE(dom.has_value());
    ASSERT_TRUE(from_toml(*dom, out).has_value());
    EXPECT_EQ(std::get<int>(out.data), 42);
}

TEST_CASE(string_roundtrip) {
    struct Holder {
        AdjTagShape data;
    };

    auto tbl = ::toml::table{
        {"data", ::toml::table{{"type", "text"}, {"value", "hello"}}}
    };
    Holder out{};
    ASSERT_TRUE(from_toml(tbl, out).has_value());
    EXPECT_EQ(std::get<std::string>(out.data), "hello");
}

TEST_CASE(unknown_tag_fails) {
    struct Holder {
        AdjTagShape data;
    };

    auto tbl = ::toml::table{
        {"data", ::toml::table{{"type", "unknown"}, {"value", 1}}}
    };
    Holder out{};
    EXPECT_FALSE(from_toml(tbl, out).has_value());
}

TEST_CASE(missing_tag_fails) {
    struct Holder {
        AdjTagShape data;
    };

    auto tbl = ::toml::table{
        {"data", ::toml::table{{"value", 42}}}
    };
    Holder out{};
    EXPECT_FALSE(from_toml(tbl, out).has_value());
}

TEST_CASE(missing_content_fails) {
    struct Holder {
        AdjTagShape data;
    };

    auto tbl = ::toml::table{
        {"data", ::toml::table{{"type", "integer"}}}
    };
    Holder out{};
    EXPECT_FALSE(from_toml(tbl, out).has_value());
}

};  // TEST_SUITE(serde_toml_variant_adjacently_tagged)

}  // namespace

}  // namespace kota::codec

#endif  // __has_include(<toml++/toml.hpp>)
