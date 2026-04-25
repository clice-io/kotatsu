#include <cstdint>
#include <limits>
#include <string>

#include "kota/zest/zest.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

namespace {

namespace json = kota::codec::json;

TEST_SUITE(content_to_json) {

// ---------------------------------------------------------------------------
// Leaf values
// ---------------------------------------------------------------------------

TEST_CASE(null_value) {
    content::Value v(nullptr);
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "null");
}

TEST_CASE(bool_true) {
    content::Value v(true);
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "true");
}

TEST_CASE(bool_false) {
    content::Value v(false);
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "false");
}

TEST_CASE(signed_int_positive) {
    content::Value v(std::int64_t{42});
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "42");
}

TEST_CASE(signed_int_negative) {
    content::Value v(std::int64_t{-100});
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "-100");
}

TEST_CASE(signed_int_zero) {
    content::Value v(std::int64_t{0});
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "0");
}

TEST_CASE(signed_int_min) {
    content::Value v(std::numeric_limits<std::int64_t>::min());
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "-9223372036854775808");
}

TEST_CASE(signed_int_max) {
    content::Value v(std::numeric_limits<std::int64_t>::max());
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "9223372036854775807");
}

TEST_CASE(unsigned_int) {
    content::Value v(std::uint64_t{123});
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "123");
}

TEST_CASE(unsigned_int_max) {
    content::Value v(std::numeric_limits<std::uint64_t>::max());
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "18446744073709551615");
}

TEST_CASE(floating_point) {
    content::Value v(3.14);
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "3.14");
}

TEST_CASE(floating_point_zero) {
    content::Value v(0.0);
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "0.0");
}

TEST_CASE(floating_point_negative) {
    content::Value v(-2.5);
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "-2.5");
}

TEST_CASE(floating_point_nan) {
    content::Value v(std::numeric_limits<double>::quiet_NaN());
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "null");
}

TEST_CASE(floating_point_inf) {
    content::Value v(std::numeric_limits<double>::infinity());
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "null");
}

TEST_CASE(floating_point_neg_inf) {
    content::Value v(-std::numeric_limits<double>::infinity());
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "null");
}

TEST_CASE(string_simple) {
    content::Value v(std::string("hello"));
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"("hello")");
}

TEST_CASE(string_empty) {
    content::Value v(std::string(""));
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"("")");
}

TEST_CASE(string_with_escapes) {
    content::Value v(std::string("a\"b\\c\n"));
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"("a\"b\\c\n")");
}

TEST_CASE(string_with_control_chars) {
    content::Value v(std::string("tab\there"));
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"("tab\there")");
}

TEST_CASE(string_with_carriage_return) {
    content::Value v(std::string("line\rend"));
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"("line\rend")");
}

TEST_CASE(string_with_backspace) {
    content::Value v(std::string("back\bspace"));
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"("back\bspace")");
}

TEST_CASE(string_with_formfeed) {
    content::Value v(std::string("form\ffeed"));
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"("form\ffeed")");
}

// ---------------------------------------------------------------------------
// Arrays
// ---------------------------------------------------------------------------

TEST_CASE(empty_array) {
    content::Value v(content::Array{});
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "[]");
}

TEST_CASE(array_single_element) {
    content::Value v(content::Array{std::int64_t{1}});
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "[1]");
}

TEST_CASE(array_multiple_elements) {
    content::Value v(content::Array{std::int64_t{1}, std::int64_t{2}, std::int64_t{3}});
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "[1,2,3]");
}

TEST_CASE(array_mixed_types) {
    content::Value v(content::Array{nullptr, true, std::int64_t{42}, 3.14, std::string("hi")});
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"([null,true,42,3.14,"hi"])");
}

TEST_CASE(array_nested) {
    content::Value v(content::Array{
        content::Array{std::int64_t{1}, std::int64_t{2}},
        std::int64_t{3},
    });
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "[[1,2],3]");
}

TEST_CASE(array_direct) {
    content::Array arr{std::int64_t{10}, std::int64_t{20}};
    auto result = json::to_string(arr);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "[10,20]");
}

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

TEST_CASE(empty_object) {
    content::Value v(content::Object{});
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "{}");
}

TEST_CASE(object_single_field) {
    content::Value v{
        {"x", std::int64_t{1}}
    };
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"({"x":1})");
}

TEST_CASE(object_multiple_fields) {
    content::Value v{
        {"name",   std::string("alice")},
        {"age",    std::int64_t{30}    },
        {"active", true                },
    };
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"({"name":"alice","age":30,"active":true})");
}

TEST_CASE(object_nested) {
    content::Value v{
        {"inner", {{"a", std::int64_t{1}}}},
        {"b",     std::int64_t{2}         },
    };
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"({"inner":{"a":1},"b":2})");
}

TEST_CASE(object_with_array_field) {
    content::Value v{
        {"items", content::Array{std::int64_t{1}, std::int64_t{2}}}
    };
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"({"items":[1,2]})");
}

TEST_CASE(object_with_null_field) {
    content::Value v{
        {"value", nullptr}
    };
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"({"value":null})");
}

TEST_CASE(object_key_needs_escape) {
    content::Value v{
        {"key\"with\"quotes", std::int64_t{1}}
    };
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"({"key\"with\"quotes":1})");
}

TEST_CASE(object_direct) {
    content::Object obj{
        {"x", std::int64_t{10}}
    };
    auto result = json::to_string(obj);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"({"x":10})");
}

// ---------------------------------------------------------------------------
// Deep nesting
// ---------------------------------------------------------------------------

TEST_CASE(deep_nesting) {
    content::Value v{
        {"data",
         {
             {"list", content::Array{{{"val", std::int64_t{42}}}, nullptr}},
             {"flag", true},
         }                          },
        {"version", std::uint64_t{1}},
    };
    auto result = json::to_string(v);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"({"data":{"list":[{"val":42},null],"flag":true},"version":1})");
}

// ---------------------------------------------------------------------------
// Round-trip: parse JSON -> content::Value -> to_string
// ---------------------------------------------------------------------------

TEST_CASE(round_trip_simple_object) {
    std::string_view input = R"({"a":1,"b":"two","c":true,"d":null})";
    auto parsed = json::parse<content::Value>(input);
    ASSERT_TRUE(parsed.has_value());
    auto output = json::to_string(*parsed);
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(*output, input);
}

TEST_CASE(round_trip_nested) {
    std::string_view input = R"({"x":{"y":[1,2,3]},"z":false})";
    auto parsed = json::parse<content::Value>(input);
    ASSERT_TRUE(parsed.has_value());
    auto output = json::to_string(*parsed);
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(*output, input);
}

TEST_CASE(round_trip_array_root) {
    std::string_view input = R"([1,"two",true,null,[3,4]])";
    auto parsed = json::parse<content::Value>(input);
    ASSERT_TRUE(parsed.has_value());
    auto output = json::to_string(*parsed);
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(*output, input);
}

};  // TEST_SUITE(content_to_json)

}  // namespace

}  // namespace kota::codec
