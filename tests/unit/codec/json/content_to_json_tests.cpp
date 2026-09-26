#include <cstdint>
#include <limits>
#include <string>

#include "kota/zest/zest.h"
#include "kota/codec/dyn/dyn.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

namespace {

namespace json = kota::codec::json;

ZEST_SUITE(content_to_json) {

// ---------------------------------------------------------------------------
// Leaf values
// ---------------------------------------------------------------------------

ZEST_CASE(null_value) {
    dyn::Value v(nullptr);
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "null");
}

ZEST_CASE(bool_true) {
    dyn::Value v(true);
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "true");
}

ZEST_CASE(bool_false) {
    dyn::Value v(false);
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "false");
}

ZEST_CASE(signed_int_positive) {
    dyn::Value v(std::int64_t{42});
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "42");
}

ZEST_CASE(signed_int_negative) {
    dyn::Value v(std::int64_t{-100});
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "-100");
}

ZEST_CASE(signed_int_zero) {
    dyn::Value v(std::int64_t{0});
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "0");
}

ZEST_CASE(signed_int_min) {
    dyn::Value v(std::numeric_limits<std::int64_t>::min());
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "-9223372036854775808");
}

ZEST_CASE(signed_int_max) {
    dyn::Value v(std::numeric_limits<std::int64_t>::max());
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "9223372036854775807");
}

ZEST_CASE(unsigned_int) {
    dyn::Value v(std::uint64_t{123});
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "123");
}

ZEST_CASE(unsigned_int_max) {
    dyn::Value v(std::numeric_limits<std::uint64_t>::max());
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "18446744073709551615");
}

ZEST_CASE(floating_point) {
    dyn::Value v(3.14);
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "3.14");
}

ZEST_CASE(floating_point_zero) {
    dyn::Value v(0.0);
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "0.0");
}

ZEST_CASE(floating_point_negative) {
    dyn::Value v(-2.5);
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "-2.5");
}

ZEST_CASE(floating_point_nan) {
    dyn::Value v(std::numeric_limits<double>::quiet_NaN());
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "null");
}

ZEST_CASE(floating_point_inf) {
    dyn::Value v(std::numeric_limits<double>::infinity());
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "null");
}

ZEST_CASE(floating_point_neg_inf) {
    dyn::Value v(-std::numeric_limits<double>::infinity());
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "null");
}

ZEST_CASE(string_simple) {
    dyn::Value v(std::string("hello"));
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == R"("hello")");
}

ZEST_CASE(string_empty) {
    dyn::Value v(std::string(""));
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == R"("")");
}

ZEST_CASE(string_with_escapes) {
    dyn::Value v(std::string("a\"b\\c\n"));
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == R"("a\"b\\c\n")");
}

ZEST_CASE(string_with_control_chars) {
    dyn::Value v(std::string("tab\there"));
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == R"("tab\there")");
}

ZEST_CASE(string_with_carriage_return) {
    dyn::Value v(std::string("line\rend"));
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == R"("line\rend")");
}

ZEST_CASE(string_with_backspace) {
    dyn::Value v(std::string("back\bspace"));
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == R"("back\bspace")");
}

ZEST_CASE(string_with_formfeed) {
    dyn::Value v(std::string("form\ffeed"));
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == R"("form\ffeed")");
}

// ---------------------------------------------------------------------------
// Arrays
// ---------------------------------------------------------------------------

ZEST_CASE(empty_array) {
    dyn::Value v(dyn::Array{});
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "[]");
}

ZEST_CASE(array_single_element) {
    dyn::Value v(dyn::Array{std::int64_t{1}});
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "[1]");
}

ZEST_CASE(array_multiple_elements) {
    dyn::Value v(dyn::Array{std::int64_t{1}, std::int64_t{2}, std::int64_t{3}});
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "[1,2,3]");
}

ZEST_CASE(array_mixed_types) {
    dyn::Value v(dyn::Array{nullptr, true, std::int64_t{42}, 3.14, std::string("hi")});
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == R"([null,true,42,3.14,"hi"])");
}

ZEST_CASE(array_nested) {
    dyn::Value v(dyn::Array{
        dyn::Array{std::int64_t{1}, std::int64_t{2}},
        std::int64_t{3},
    });
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "[[1,2],3]");
}

ZEST_CASE(array_direct) {
    dyn::Array arr{std::int64_t{10}, std::int64_t{20}};
    auto result = json::to_string(arr);
    ASSERT(result);
    EXPECT(*result == "[10,20]");
}

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

ZEST_CASE(empty_object) {
    dyn::Value v(dyn::Object{});
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == "{}");
}

ZEST_CASE(object_single_field) {
    dyn::Value v{
        {"x", std::int64_t{1}}
    };
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == R"({"x":1})");
}

ZEST_CASE(object_multiple_fields) {
    dyn::Value v{
        {"name",   std::string("alice")},
        {"age",    std::int64_t{30}    },
        {"active", true                },
    };
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == R"({"name":"alice","age":30,"active":true})");
}

ZEST_CASE(object_nested) {
    dyn::Value v{
        {"inner", {{"a", std::int64_t{1}}}},
        {"b",     std::int64_t{2}         },
    };
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == R"({"inner":{"a":1},"b":2})");
}

ZEST_CASE(object_with_array_field) {
    dyn::Value v{
        {"items", dyn::Array{std::int64_t{1}, std::int64_t{2}}}
    };
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == R"({"items":[1,2]})");
}

ZEST_CASE(object_with_null_field) {
    dyn::Value v{
        {"value", nullptr}
    };
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == R"({"value":null})");
}

ZEST_CASE(object_key_needs_escape) {
    dyn::Value v{
        {"key\"with\"quotes", std::int64_t{1}}
    };
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == R"({"key\"with\"quotes":1})");
}

ZEST_CASE(object_direct) {
    dyn::Object obj{
        {"x", std::int64_t{10}}
    };
    auto result = json::to_string(obj);
    ASSERT(result);
    EXPECT(*result == R"({"x":10})");
}

// ---------------------------------------------------------------------------
// Deep nesting
// ---------------------------------------------------------------------------

ZEST_CASE(deep_nesting) {
    dyn::Value v{
        {"data",
         {
             {"list", dyn::Array{{{"val", std::int64_t{42}}}, nullptr}},
             {"flag", true},
         }                          },
        {"version", std::uint64_t{1}},
    };
    auto result = json::to_string(v);
    ASSERT(result);
    EXPECT(*result == R"({"data":{"list":[{"val":42},null],"flag":true},"version":1})");
}

// ---------------------------------------------------------------------------
// Round-trip: parse JSON -> dyn::Value -> to_string
// ---------------------------------------------------------------------------

ZEST_CASE(round_trip_simple_object) {
    std::string_view input = R"({"a":1,"b":"two","c":true,"d":null})";
    auto parsed = json::from_string<dyn::Value>(input);
    ASSERT(parsed);
    auto output = json::to_string(*parsed);
    ASSERT(output);
    EXPECT(*output == input);
}

ZEST_CASE(round_trip_nested) {
    std::string_view input = R"({"x":{"y":[1,2,3]},"z":false})";
    auto parsed = json::from_string<dyn::Value>(input);
    ASSERT(parsed);
    auto output = json::to_string(*parsed);
    ASSERT(output);
    EXPECT(*output == input);
}

ZEST_CASE(round_trip_array_root) {
    std::string_view input = R"([1,"two",true,null,[3,4]])";
    auto parsed = json::from_string<dyn::Value>(input);
    ASSERT(parsed);
    auto output = json::to_string(*parsed);
    ASSERT(output);
    EXPECT(*output == input);
}

ZEST_CASE(integer_beyond_64_bits_reads_as_double) {
    auto parsed = json::from_string<dyn::Value>(R"([18446744073709551616,-9223372036854775809])");
    ASSERT(parsed);
    EXPECT((*parsed)[0].get_double() == 18446744073709551616.0);
    EXPECT((*parsed)[1].get_double() == -9223372036854775809.0);
}

};  // ZEST_SUITE(content_to_json)

}  // namespace

}  // namespace kota::codec
