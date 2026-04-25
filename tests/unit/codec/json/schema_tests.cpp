#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/codec/json/schema.h"

namespace kota::meta {

struct json_schema_opaque_root {};

}  // namespace kota::meta

namespace kota::meta {

template <>
constexpr inline bool schema_opaque<kota::meta::json_schema_opaque_root> = true;

}  // namespace kota::meta

namespace kota::meta {

namespace {

// ---------------------------------------------------------------------------
// Scalar wrappers
// ---------------------------------------------------------------------------
struct s_bool {
    bool v;
};

struct s_i8 {
    std::int8_t v;
};

struct s_i16 {
    std::int16_t v;
};

struct s_i32 {
    std::int32_t v;
};

struct s_i64 {
    std::int64_t v;
};

struct s_u8 {
    std::uint8_t v;
};

struct s_u16 {
    std::uint16_t v;
};

struct s_u32 {
    std::uint32_t v;
};

struct s_u64 {
    std::uint64_t v;
};

struct s_f32 {
    float v;
};

struct s_f64 {
    double v;
};

struct s_char {
    char v;
};

struct s_str {
    std::string v;
};

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------
enum class color_i8 : std::int8_t { red = 0, green = 1, blue = 2 };
enum class single_enum : std::int32_t { only = 42 };
enum class status : std::int32_t { ok = 0, fail = 1, pending = 2 };
enum class flag_u8 : std::uint8_t { off = 0, on = 1 };
enum class level_i16 : std::int16_t { low = 0, mid = 50, high = 100 };

// ---------------------------------------------------------------------------
// Containers
// ---------------------------------------------------------------------------
struct s_vec_i32 {
    std::vector<std::int32_t> v;
};

struct s_set_i32 {
    std::set<std::int32_t> v;
};

struct s_map_str_i32 {
    std::map<std::string, std::int32_t> v;
};

struct s_vec_vec_i32 {
    std::vector<std::vector<std::int32_t>> v;
};

struct s_map_str_vec_i32 {
    std::map<std::string, std::vector<std::int32_t>> v;
};

// ---------------------------------------------------------------------------
// Tuple / Pair
// ---------------------------------------------------------------------------
struct s_pair {
    std::pair<std::string, std::int32_t> v;
};

struct s_tuple {
    std::tuple<std::int32_t, std::string, bool> v;
};

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------
struct empty_struct {};

struct single_field {
    std::int32_t x;
};

struct point2d {
    std::int32_t x;
    std::int32_t y;
};

struct with_string {
    std::string name;
    std::int32_t value;
};

// ---------------------------------------------------------------------------
// Nested structs
// ---------------------------------------------------------------------------
struct inner {
    std::int32_t a;
};

struct middle {
    inner i;
    std::string s;
};

struct outer {
    middle m;
    std::int32_t n;
};

// ---------------------------------------------------------------------------
// Struct with enum
// ---------------------------------------------------------------------------
struct with_enum {
    color_i8 c;
    std::string name;
};

// ---------------------------------------------------------------------------
// Optional / pointer
// ---------------------------------------------------------------------------
struct with_optional {
    std::string name;
    std::optional<std::int32_t> age;
};

struct with_unique {
    std::string name;
    std::unique_ptr<std::int32_t> ptr;
};

struct with_shared {
    std::string name;
    std::shared_ptr<std::int32_t> ptr;
};

// ---------------------------------------------------------------------------
// Attributes
// ---------------------------------------------------------------------------
struct with_default {
    std::string name;
    annotation<std::int32_t, attrs::default_value> count;
};

struct with_skip {
    std::string visible;
    annotation<std::int32_t, attrs::skip> hidden;
};

struct base_fields {
    std::int32_t a;
    std::int32_t b;
};

struct with_flatten {
    annotation<base_fields, attrs::flatten> base;
    std::string extra;
};

struct with_rename {
    annotation<std::int32_t, attrs::rename<"my_field">> x;
    std::string y;
};

// ---------------------------------------------------------------------------
// Variant
// ---------------------------------------------------------------------------
struct var_none {
    std::variant<std::int32_t, std::string> v;
};

struct var_three {
    std::variant<std::int32_t, std::string, bool> v;
};

struct tagged_circle {
    double radius;
};

struct tagged_rect {
    double width;
    double height;
};

using root_external_variant = annotation<std::variant<std::int32_t, std::string>,
                                         attrs::externally_tagged::names<"integer", "text">>;

using root_internal_variant = annotation<std::variant<tagged_circle, tagged_rect>,
                                         attrs::internally_tagged<"kind">::names<"circle", "rect">>;

using root_adjacent_variant =
    annotation<std::variant<std::int32_t, std::string>,
               attrs::adjacently_tagged<"type", "value">::names<"integer", "text">>;

// ---------------------------------------------------------------------------
// Combinations
// ---------------------------------------------------------------------------
struct combo {
    color_i8 color;
    std::optional<std::string> label;
    std::vector<std::int32_t> values;
    std::map<std::string, std::int32_t> attrs;
};

struct nested_combo {
    point2d point;
    color_i8 color;
    std::vector<point2d> points;
    std::map<std::string, point2d> named_points;
};

struct multi_map {
    std::map<std::string, std::int32_t> a;
    std::map<std::string, std::string> b;
};

struct vec_of_struct {
    std::vector<point2d> items;
};

struct deep_inner {
    color_i8 c;
    std::int32_t v;
};

struct deep_middle {
    deep_inner di;
    std::string s;
};

struct deep_outer {
    deep_middle dm;
    std::int32_t n;
};

// ---------------------------------------------------------------------------
// Additional types
// ---------------------------------------------------------------------------
struct all_optional {
    std::optional<std::int32_t> a;
    std::optional<std::string> b;
};

struct all_default {
    annotation<std::int32_t, attrs::default_value> x;
    annotation<std::string, attrs::default_value> y;
};

struct skip_default {
    std::string name;
    annotation<std::int32_t, attrs::skip> hidden;
    annotation<std::int32_t, attrs::default_value> count;
};

struct base_with_opt {
    std::int32_t x;
    std::optional<std::int32_t> y;
};

struct flatten_opt {
    annotation<base_with_opt, attrs::flatten> base;
    std::string tag;
};

struct rename_base {
    annotation<std::int32_t, attrs::rename<"alpha">> a;
    std::int32_t b;
};

struct flatten_rename {
    annotation<rename_base, attrs::flatten> inner;
    std::string extra;
};

struct map_str_struct {
    std::map<std::string, point2d> entries;
};

struct map_str_enum {
    std::map<std::string, color_i8> entries;
};

struct vec_optional {
    std::vector<std::optional<std::int32_t>> v;
};

struct optional_vec {
    std::optional<std::vector<std::int32_t>> v;
};

struct with_pair_field {
    std::pair<std::string, std::int32_t> p;
    std::string name;
};

struct with_tuple_field {
    std::tuple<std::int32_t, bool> t;
    std::string name;
};

struct shared_struct {
    std::string name;
    std::shared_ptr<point2d> point;
};

struct multi_ref {
    point2d a;
    point2d b;
    std::vector<point2d> list;
};

struct vec_enum {
    std::vector<color_i8> colors;
};

struct set_string {
    std::set<std::string> tags;
};

struct optional_struct {
    std::optional<point2d> point;
    std::string name;
};

struct vec_map {
    std::vector<std::map<std::string, std::int32_t>> items;
};

struct map_vec_struct {
    std::map<std::string, std::vector<point2d>> groups;
};

struct trivial_nested {
    point2d p;
    std::int32_t z;
};

struct multi_enum {
    color_i8 c;
    status s;
    std::string label;
};

struct with_flag {
    flag_u8 f;
    std::string name;
};

struct with_level {
    level_i16 l;
    std::int32_t v;
};

struct with_all_ptr {
    std::optional<std::string> opt;
    std::unique_ptr<std::int32_t> uniq;
    std::shared_ptr<bool> shr;
};

struct deep_container {
    std::map<std::string, std::vector<std::map<std::string, std::int32_t>>> data;
};

struct optional_inner {
    std::optional<inner> i;
    std::string name;
};

struct map_of_map {
    std::map<std::string, std::map<std::string, std::int32_t>> m;
};

struct vec_variant {
    std::vector<std::variant<std::int32_t, std::string>> items;
};

struct many_fields {
    std::int32_t a;
    std::int32_t b;
    std::int32_t c;
    std::string d;
    bool e;
    double f;
};

struct set_of_struct {
    std::set<std::int32_t> ids;
    std::string name;
};

namespace json = kota::codec::json;

template <typename T>
void check_root_integer_schema() {
    const auto result = json::schema_string<T>().value();
    std::string expected;
    if constexpr(std::is_signed_v<T>) {
        expected = std::format(
            R"({{"$schema":"https://json-schema.org/draft/2020-12/schema","type":"integer","minimum":{},"maximum":{}}})",
            static_cast<std::int64_t>(std::numeric_limits<T>::min()),
            static_cast<std::int64_t>(std::numeric_limits<T>::max()));
    } else {
        expected = std::format(
            R"({{"$schema":"https://json-schema.org/draft/2020-12/schema","type":"integer","minimum":0,"maximum":{}}})",
            static_cast<std::uint64_t>(std::numeric_limits<T>::max()));
    }
    EXPECT_EQ(result, expected);
}

template <typename Wrapper, typename T>
void check_wrapper_integer_schema() {
    const auto result = json::schema_string<Wrapper>().value();
    std::string expected;
    if constexpr(std::is_signed_v<T>) {
        expected = std::format(
            R"({{"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","properties":{{"v":{{"type":"integer","minimum":{},"maximum":{}}}}},"required":["v"]}})",
            static_cast<std::int64_t>(std::numeric_limits<T>::min()),
            static_cast<std::int64_t>(std::numeric_limits<T>::max()));
    } else {
        expected = std::format(
            R"({{"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","properties":{{"v":{{"type":"integer","minimum":0,"maximum":{}}}}},"required":["v"]}})",
            static_cast<std::uint64_t>(std::numeric_limits<T>::max()));
    }
    EXPECT_EQ(result, expected);
}

TEST_SUITE(serde_json_schema) {

// ---------------------------------------------------------------------------
// Root scalars
// ---------------------------------------------------------------------------

TEST_CASE(root_bool) {
    const auto result = json::schema_string<bool>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"boolean"})");
}

TEST_CASE(root_integers) {
    check_root_integer_schema<std::int8_t>();
    check_root_integer_schema<std::int16_t>();
    check_root_integer_schema<std::int32_t>();
    check_root_integer_schema<std::int64_t>();
    check_root_integer_schema<std::uint8_t>();
    check_root_integer_schema<std::uint16_t>();
    check_root_integer_schema<std::uint32_t>();
    check_root_integer_schema<std::uint64_t>();
}

TEST_CASE(root_floats) {
    const auto schema =
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"number"})";
    EXPECT_EQ(json::schema_string<float>().value(), schema);
    EXPECT_EQ(json::schema_string<double>().value(), schema);
}

TEST_CASE(root_char) {
    const auto result = json::schema_string<char>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"string"})");
}

TEST_CASE(root_string) {
    const auto result = json::schema_string<std::string>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"string"})");
}

// ---------------------------------------------------------------------------
// Scalar struct wrappers
// ---------------------------------------------------------------------------

TEST_CASE(scalar_wrapper_bool) {
    const auto result = json::schema_string<s_bool>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"boolean"}},)" R"("required":["v"]})");
}

TEST_CASE(scalar_wrapper_integers) {
    check_wrapper_integer_schema<s_i8, std::int8_t>();
    check_wrapper_integer_schema<s_i16, std::int16_t>();
    check_wrapper_integer_schema<s_i32, std::int32_t>();
    check_wrapper_integer_schema<s_i64, std::int64_t>();
    check_wrapper_integer_schema<s_u8, std::uint8_t>();
    check_wrapper_integer_schema<s_u16, std::uint16_t>();
    check_wrapper_integer_schema<s_u32, std::uint32_t>();
    check_wrapper_integer_schema<s_u64, std::uint64_t>();
}

TEST_CASE(scalar_wrapper_floats) {
    const auto schema =
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","properties":{"v":{"type":"number"}},"required":["v"]})";
    EXPECT_EQ(json::schema_string<s_f32>().value(), schema);
    EXPECT_EQ(json::schema_string<s_f64>().value(), schema);
}

TEST_CASE(scalar_wrapper_char) {
    const auto result = json::schema_string<s_char>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"string"}},)" R"("required":["v"]})");
}

TEST_CASE(scalar_wrapper_str) {
    const auto result = json::schema_string<s_str>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"string"}},)" R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Root enums
// ---------------------------------------------------------------------------

TEST_CASE(root_enum_color_i8) {
    const auto result = json::schema_string<color_i8>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("enum":["red","green","blue"]})");
}

TEST_CASE(root_enum_single) {
    const auto result = json::schema_string<single_enum>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("enum":["only"]})");
}

TEST_CASE(root_enum_status) {
    const auto result = json::schema_string<status>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("enum":["ok","fail","pending"]})");
}

TEST_CASE(root_enum_flag_u8) {
    const auto result = json::schema_string<flag_u8>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("enum":["off","on"]})");
}

TEST_CASE(root_enum_level_i16) {
    const auto result = json::schema_string<level_i16>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("enum":["low","mid","high"]})");
}

// ---------------------------------------------------------------------------
// Containers
// ---------------------------------------------------------------------------

TEST_CASE(container_vec_i32) {
    const auto result = json::schema_string<s_vec_i32>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"array",)" R"("items":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}},)" R"("required":["v"]})");
}

TEST_CASE(container_set_i32) {
    const auto result = json::schema_string<s_set_i32>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"array",)" R"("items":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("uniqueItems":true}},)" R"("required":["v"]})");
}

TEST_CASE(container_map_str_i32) {
    const auto result = json::schema_string<s_map_str_i32>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"object",)" R"("additionalProperties":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}},)" R"("required":["v"]})");
}

TEST_CASE(container_vec_vec_i32) {
    const auto result = json::schema_string<s_vec_vec_i32>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"array",)" R"("items":{"type":"array",)" R"("items":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}}},)" R"("required":["v"]})");
}

TEST_CASE(map_str_vec_i32) {
    const auto result = json::schema_string<s_map_str_vec_i32>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"object",)" R"("additionalProperties":{"type":"array",)" R"("items":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}}},)" R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Tuple / Pair
// ---------------------------------------------------------------------------

TEST_CASE(tuple_pair) {
    const auto result = json::schema_string<s_pair>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"array",)" R"("prefixItems":[)" R"({"type":"string"},)" R"({"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}],)" R"("items":false}},)" R"("required":["v"]})");
}

TEST_CASE(tuple_triple) {
    const auto result = json::schema_string<s_tuple>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"array",)" R"("prefixItems":[)" R"({"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"string"},)" R"({"type":"boolean"}],)" R"("items":false}},)" R"("required":["v"]})");
}

TEST_CASE(tuple_pair_in_struct) {
    const auto result = json::schema_string<with_pair_field>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("p":{"type":"array",)" R"("prefixItems":[)" R"({"type":"string"},)" R"({"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}],)" R"("items":false},)" R"("name":{"type":"string"}},)" R"("required":["p","name"]})");
}

TEST_CASE(tuple_in_struct) {
    const auto result = json::schema_string<with_tuple_field>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("t":{"type":"array",)" R"("prefixItems":[)" R"({"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"boolean"}],)" R"("items":false},)" R"("name":{"type":"string"}},)" R"("required":["t","name"]})");
}

// ---------------------------------------------------------------------------
// Basic structs
// ---------------------------------------------------------------------------

TEST_CASE(struct_empty) {
    const auto result = json::schema_string<empty_struct>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{}})");
}

TEST_CASE(struct_single_field) {
    const auto result = json::schema_string<single_field>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x"]})");
}

TEST_CASE(struct_point2d) {
    const auto result = json::schema_string<point2d>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]})");
}

TEST_CASE(struct_with_string) {
    const auto result = json::schema_string<with_string>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("value":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["name","value"]})");
}

// ---------------------------------------------------------------------------
// Nested structs
// ---------------------------------------------------------------------------

TEST_CASE(nested_inner) {
    const auto result = json::schema_string<inner>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("a":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["a"]})");
}

TEST_CASE(nested_middle) {
    const auto result = json::schema_string<middle>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("i":{"$ref":"#/$defs/inner"},)" R"("s":{"type":"string"}},)" R"("required":["i","s"],)" R"("$defs":{)" R"("inner":{"type":"object",)" R"("properties":{)" R"("a":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["a"]}}})");
}

TEST_CASE(nested_outer) {
    const auto result = json::schema_string<outer>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("m":{"$ref":"#/$defs/middle"},)" R"("n":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["m","n"],)" R"("$defs":{)" R"("inner":{"type":"object",)" R"("properties":{)" R"("a":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["a"]},)" R"("middle":{"type":"object",)" R"("properties":{)" R"("i":{"$ref":"#/$defs/inner"},)" R"("s":{"type":"string"}},)" R"("required":["i","s"]}}})");
}

TEST_CASE(nested_with_enum) {
    const auto result = json::schema_string<with_enum>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("c":{"enum":["red","green","blue"]},)" R"("name":{"type":"string"}},)" R"("required":["c","name"]})");
}

// ---------------------------------------------------------------------------
// Optional / pointer
// ---------------------------------------------------------------------------

TEST_CASE(optional_field) {
    const auto result = json::schema_string<with_optional>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("age":{"oneOf":[{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"null"}]}},)" R"("required":["name"]})");
}

TEST_CASE(unique_ptr_field) {
    const auto result = json::schema_string<with_unique>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("ptr":{"oneOf":[{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"null"}]}},)" R"("required":["name"]})");
}

TEST_CASE(shared_ptr_field) {
    const auto result = json::schema_string<with_shared>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("ptr":{"oneOf":[{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"null"}]}},)" R"("required":["name"]})");
}

TEST_CASE(all_optional_fields) {
    const auto result = json::schema_string<all_optional>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("a":{"oneOf":[{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"null"}]},)" R"("b":{"oneOf":[{"type":"string"},)" R"({"type":"null"}]}}})");
}

TEST_CASE(all_ptr_types) {
    const auto result = json::schema_string<with_all_ptr>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("opt":{"oneOf":[{"type":"string"},)" R"({"type":"null"}]},)" R"("uniq":{"oneOf":[{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"null"}]},)" R"("shr":{"oneOf":[{"type":"boolean"},)" R"({"type":"null"}]}}})");
}

// ---------------------------------------------------------------------------
// default_value attribute
// ---------------------------------------------------------------------------

TEST_CASE(attr_default_value) {
    const auto result = json::schema_string<with_default>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("count":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["name"]})");
}

TEST_CASE(all_default_fields) {
    const auto result = json::schema_string<all_default>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"string"}}})");
}

// ---------------------------------------------------------------------------
// deny_unknown_fields
// ---------------------------------------------------------------------------

TEST_CASE(deny_unknown_struct) {
    const static field_info deny_fields[] = {
        {"name",  {}, 0, 0, type_info_of<std::string>,  false, false, false, false},
        {"count", {}, 0, 1, type_info_of<std::int32_t>, false, false, false, false},
    };
    const static struct_type_info deny_info = {
        {type_kind::structure, "deny_struct"},
        true, // deny_unknown
        false, // is_trivial_layout
        {deny_fields,          2            },
    };
    const auto result = json::schema_string(deny_info).value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("count":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["name","count"],)" R"("additionalProperties":false})");
}

// ---------------------------------------------------------------------------
// skip
// ---------------------------------------------------------------------------

TEST_CASE(attr_skip) {
    const auto result = json::schema_string<with_skip>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("visible":{"type":"string"}},)" R"("required":["visible"]})");
}

TEST_CASE(skip_and_default) {
    const auto result = json::schema_string<skip_default>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("count":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["name"]})");
}

// ---------------------------------------------------------------------------
// flatten
// ---------------------------------------------------------------------------

TEST_CASE(attr_flatten) {
    const auto result = json::schema_string<with_flatten>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("a":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("b":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("extra":{"type":"string"}},)" R"("required":["a","b","extra"]})");
}

TEST_CASE(flatten_with_optional) {
    const auto result = json::schema_string<flatten_opt>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"oneOf":[{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"null"}]},)" R"("tag":{"type":"string"}},)" R"("required":["x","tag"]})");
}

TEST_CASE(flatten_with_rename) {
    const auto result = json::schema_string<flatten_rename>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("alpha":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("b":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("extra":{"type":"string"}},)" R"("required":["alpha","b","extra"]})");
}

// ---------------------------------------------------------------------------
// rename
// ---------------------------------------------------------------------------

TEST_CASE(attr_rename) {
    const auto result = json::schema_string<with_rename>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("my_field":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"string"}},)" R"("required":["my_field","y"]})");
}

// ---------------------------------------------------------------------------
// Variant (tag_mode::none)
// ---------------------------------------------------------------------------

TEST_CASE(variant_untagged) {
    const auto result = json::schema_string<var_none>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"oneOf":[)" R"({"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"string"}]}},)" R"("required":["v"]})");
}

TEST_CASE(variant_three_alts) {
    const auto result = json::schema_string<var_three>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"oneOf":[)" R"({"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"string"},)" R"({"type":"boolean"}]}},)" R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Variant (tag_mode::external)
// ---------------------------------------------------------------------------

TEST_CASE(variant_external_tag) {
    const static type_info_fn ext_alts[] = {
        type_info_of<std::int32_t>,
        type_info_of<std::string>,
    };
    const static std::string_view ext_names[] = {"num", "text"};
    const static variant_type_info ext_var = {
        {type_kind::variant, "ext_var"},
        {ext_alts, 2},
        tag_mode::external,
        {},
        {},
        {ext_names, 2},
    };
    const static type_info_fn ext_var_ref = []() -> const type_info& {
        return ext_var;
    };
    const static field_info ext_field = {
        "v",
        {},
        0,
        0,
        ext_var_ref,
        false,
        false,
        false,
        false,
    };
    const static struct_type_info ext_wrap = {
        {type_kind::structure, "ext_wrap"},
        false,
        false,
        {&ext_field,           1         },
    };
    const auto result = json::schema_string(ext_wrap).value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"oneOf":[)" R"({"type":"object",)" R"("properties":{)" R"("num":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["num"],)" R"("additionalProperties":false},)" R"({"type":"object",)" R"("properties":{)" R"("text":{"type":"string"}},)" R"("required":["text"],)" R"("additionalProperties":false}]}},)" R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Variant (tag_mode::internal)
// ---------------------------------------------------------------------------

TEST_CASE(variant_internal_tag) {
    const static type_info_fn int_alts[] = {
        type_info_of<point2d>,
        type_info_of<inner>,
    };
    const static std::string_view int_names[] = {"point", "inner"};
    const static variant_type_info int_var = {
        {type_kind::variant, "int_var"},
        {int_alts, 2},
        tag_mode::internal,
        "type",
        {},
        {int_names, 2},
    };
    const static type_info_fn int_var_ref = []() -> const type_info& {
        return int_var;
    };
    const static field_info int_field = {
        "v",
        {},
        0,
        0,
        int_var_ref,
        false,
        false,
        false,
        false,
    };
    const static struct_type_info int_wrap = {
        {type_kind::structure, "int_wrap"},
        false,
        false,
        {&int_field,           1         },
    };
    const auto result = json::schema_string(int_wrap).value();
    EXPECT_EQ(result,
              R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"oneOf":[)" R"({"allOf":[)" R"({"$ref":"#/$defs/point2d"},)" R"({"properties":{)" R"("type":{"const":"point"}},)" R"("required":["type"]}]},)" R"({"allOf":[)" R"({"$ref":"#/$defs/inner"},)" R"({"properties":{)" R"("type":{"const":"inner"}},)" R"("required":["type"]}]}]}},)" R"("required":["v"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]},)" R"("inner":{"type":"object",)" R"("properties":{)" R"("a":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["a"]}}})");
}

// ---------------------------------------------------------------------------
// Variant (tag_mode::adjacent)
// ---------------------------------------------------------------------------

TEST_CASE(variant_adjacent_tag) {
    const static type_info_fn adj_alts[] = {
        type_info_of<std::int32_t>,
        type_info_of<std::string>,
    };
    const static std::string_view adj_names[] = {"num", "text"};
    const static variant_type_info adj_var = {
        {type_kind::variant, "adj_var"},
        {adj_alts,           2        },
        tag_mode::adjacent,
        "t",
        "c",
        {adj_names,          2        },
    };
    const static type_info_fn adj_var_ref = []() -> const type_info& {
        return adj_var;
    };
    const static field_info adj_field = {
        "v",
        {},
        0,
        0,
        adj_var_ref,
        false,
        false,
        false,
        false,
    };
    const static struct_type_info adj_wrap = {
        {type_kind::structure, "adj_wrap"},
        false,
        false,
        {&adj_field,           1         },
    };
    const auto result = json::schema_string(adj_wrap).value();
    EXPECT_EQ(result, R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"oneOf":[)" R"({"type":"object",)" R"("properties":{)" R"("t":{"const":"num"},)" R"("c":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["t","c"],)" R"("additionalProperties":false},)" R"({"type":"object",)" R"("properties":{)" R"("t":{"const":"text"},)" R"("c":{"type":"string"}},)" R"("required":["t","c"],)" R"("additionalProperties":false}]}},)" R"("required":["v"]})");
}

TEST_CASE(root_external_variant) {
    const auto result = json::schema_string<root_external_variant>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("oneOf":[)" R"({"type":"object",)" R"("properties":{)" R"("integer":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["integer"],)" R"("additionalProperties":false},)" R"({"type":"object",)" R"("properties":{)" R"("text":{"type":"string"}},)" R"("required":["text"],)" R"("additionalProperties":false}]})");
}

TEST_CASE(root_internal_variant) {
    const auto result = json::schema_string<root_internal_variant>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("oneOf":[)" R"({"allOf":[)" R"({"$ref":"#/$defs/tagged_circle"},)" R"({"properties":{)" R"("kind":{"const":"circle"}},)" R"("required":["kind"]}]},)" R"({"allOf":[)" R"({"$ref":"#/$defs/tagged_rect"},)" R"({"properties":{)" R"("kind":{"const":"rect"}},)" R"("required":["kind"]}]}],)" R"("$defs":{)" R"("tagged_circle":{"type":"object",)" R"("properties":{)" R"("radius":{"type":"number"}},)" R"("required":["radius"]},)" R"("tagged_rect":{"type":"object",)" R"("properties":{)" R"("width":{"type":"number"},)" R"("height":{"type":"number"}},)" R"("required":["width","height"]}}})");
}

TEST_CASE(root_adjacent_variant) {
    const auto result = json::schema_string<root_adjacent_variant>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("oneOf":[)" R"({"type":"object",)" R"("properties":{)" R"("type":{"const":"integer"},)" R"("value":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["type","value"],)" R"("additionalProperties":false},)" R"({"type":"object",)" R"("properties":{)" R"("type":{"const":"text"},)" R"("value":{"type":"string"}},)" R"("required":["type","value"],)" R"("additionalProperties":false}]})");
}

TEST_CASE(opaque_root) {
    const auto result = json::schema_string<json_schema_opaque_root>().value();
    EXPECT_EQ(result, R"({"$schema":"https://json-schema.org/draft/2020-12/schema"})");
}

// ---------------------------------------------------------------------------
// More containers
// ---------------------------------------------------------------------------

TEST_CASE(map_str_struct) {
    const auto result = json::schema_string<map_str_struct>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("entries":{"type":"object",)" R"("additionalProperties":{)" R"("$ref":"#/$defs/point2d"}}},)" R"("required":["entries"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

TEST_CASE(map_str_enum) {
    const auto result = json::schema_string<map_str_enum>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("entries":{"type":"object",)" R"("additionalProperties":{)" R"("enum":["red","green","blue"]}}},)" R"("required":["entries"]})");
}

TEST_CASE(vec_optional_items) {
    const auto result = json::schema_string<vec_optional>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"array",)" R"("items":{"oneOf":[{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"null"}]}}},)" R"("required":["v"]})");
}

TEST_CASE(optional_vec_field) {
    const auto result = json::schema_string<optional_vec>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"oneOf":[{"type":"array",)" R"("items":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"({"type":"null"}]}}})");
}

TEST_CASE(vec_of_enum) {
    const auto result = json::schema_string<vec_enum>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("colors":{"type":"array",)" R"("items":{)" R"("enum":["red","green","blue"]}}},)" R"("required":["colors"]})");
}

TEST_CASE(set_of_string) {
    const auto result = json::schema_string<set_string>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("tags":{"type":"array",)" R"("items":{"type":"string"},)" R"("uniqueItems":true}},)" R"("required":["tags"]})");
}

TEST_CASE(vec_of_map) {
    const auto result = json::schema_string<vec_map>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("items":{"type":"array",)" R"("items":{"type":"object",)" R"("additionalProperties":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}}},)" R"("required":["items"]})");
}

TEST_CASE(map_of_vec_struct) {
    const auto result = json::schema_string<map_vec_struct>().value();
    EXPECT_EQ(result, R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("groups":{"type":"object",)" R"("additionalProperties":{"type":"array",)" R"("items":{)" R"("$ref":"#/$defs/point2d"}}}},)" R"("required":["groups"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

TEST_CASE(deep_container_field) {
    const auto result = json::schema_string<deep_container>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("data":{"type":"object",)" R"("additionalProperties":{"type":"array",)" R"("items":{"type":"object",)" R"("additionalProperties":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}}}},)" R"("required":["data"]})");
}

TEST_CASE(map_of_map_field) {
    const auto result = json::schema_string<map_of_map>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("m":{"type":"object",)" R"("additionalProperties":{"type":"object",)" R"("additionalProperties":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}}},)" R"("required":["m"]})");
}

// ---------------------------------------------------------------------------
// Struct with pointer to struct
// ---------------------------------------------------------------------------

TEST_CASE(shared_ptr_to_struct) {
    const auto result = json::schema_string<shared_struct>().value();
    EXPECT_EQ(result,
              R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("point":{"oneOf":[{)" R"("$ref":"#/$defs/point2d"},)" R"({"type":"null"}]}},)" R"("required":["name"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

TEST_CASE(optional_struct_field) {
    const auto result = json::schema_string<optional_struct>().value();
    EXPECT_EQ(result,
              R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("point":{"oneOf":[{)" R"("$ref":"#/$defs/point2d"},)" R"({"type":"null"}]},)" R"("name":{"type":"string"}},)" R"("required":["name"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

// ---------------------------------------------------------------------------
// $defs dedup
// ---------------------------------------------------------------------------

TEST_CASE(defs_dedup_multi_ref) {
    const auto result = json::schema_string<multi_ref>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("a":{)" R"("$ref":"#/$defs/point2d"},)" R"("b":{)" R"("$ref":"#/$defs/point2d"},)" R"("list":{"type":"array",)" R"("items":{)" R"("$ref":"#/$defs/point2d"}}},)" R"("required":["a","b","list"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

// ---------------------------------------------------------------------------
// Struct with enum fields
// ---------------------------------------------------------------------------

TEST_CASE(multi_enum_fields) {
    const auto result = json::schema_string<multi_enum>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("c":{"enum":["red","green","blue"]},)" R"("s":{"enum":["ok","fail","pending"]},)" R"("label":{"type":"string"}},)" R"("required":["c","s","label"]})");
}

TEST_CASE(with_flag_enum) {
    const auto result = json::schema_string<with_flag>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("f":{"enum":["off","on"]},)" R"("name":{"type":"string"}},)" R"("required":["f","name"]})");
}

TEST_CASE(with_level_enum) {
    const auto result = json::schema_string<with_level>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("l":{"enum":["low","mid","high"]},)" R"("v":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["l","v"]})");
}

// ---------------------------------------------------------------------------
// Nested struct with optional
// ---------------------------------------------------------------------------

TEST_CASE(optional_inner_field) {
    const auto result = json::schema_string<optional_inner>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("i":{"oneOf":[{)" R"("$ref":"#/$defs/inner"},)" R"({"type":"null"}]},)" R"("name":{"type":"string"}},)" R"("required":["name"],)" R"("$defs":{)" R"("inner":{"type":"object",)" R"("properties":{)" R"("a":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["a"]}}})");
}

// ---------------------------------------------------------------------------
// Variant in container
// ---------------------------------------------------------------------------

TEST_CASE(vec_of_variant) {
    const auto result = json::schema_string<vec_variant>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("items":{"type":"array",)" R"("items":{"oneOf":[)" R"({"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"string"}]}}},)" R"("required":["items"]})");
}

// ---------------------------------------------------------------------------
// Combinations
// ---------------------------------------------------------------------------

TEST_CASE(combo_mixed_fields) {
    const auto result = json::schema_string<combo>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("color":{)" R"("enum":["red","green","blue"]},)" R"("label":{"oneOf":[{"type":"string"},)" R"({"type":"null"}]},)" R"("values":{"type":"array",)" R"("items":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("attrs":{"type":"object",)" R"("additionalProperties":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}},)" R"("required":["color","values","attrs"]})");
}

TEST_CASE(combo_nested_struct_refs) {
    const auto result = json::schema_string<nested_combo>().value();
    EXPECT_EQ(result,
              R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("point":{)" R"("$ref":"#/$defs/point2d"},)" R"("color":{)" R"("enum":["red","green","blue"]},)" R"("points":{"type":"array",)" R"("items":{)" R"("$ref":"#/$defs/point2d"}},)" R"("named_points":{"type":"object",)" R"("additionalProperties":{)" R"("$ref":"#/$defs/point2d"}}},)" R"("required":[)" R"("point","color","points","named_points"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

TEST_CASE(combo_vec_of_struct) {
    const auto result = json::schema_string<vec_of_struct>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("items":{"type":"array",)" R"("items":{)" R"("$ref":"#/$defs/point2d"}}},)" R"("required":["items"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

TEST_CASE(combo_deep_nesting) {
    const auto result = json::schema_string<deep_outer>().value();
    EXPECT_EQ(result,
              R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("dm":{)" R"("$ref":"#/$defs/deep_middle"},)" R"("n":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["dm","n"],)" R"("$defs":{)" R"("deep_inner":{"type":"object",)" R"("properties":{)" R"("c":{)" R"("enum":["red","green","blue"]},)" R"("v":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["c","v"]},)" R"("deep_middle":{"type":"object",)" R"("properties":{)" R"("di":{)" R"("$ref":"#/$defs/deep_inner"},)" R"("s":{"type":"string"}},)" R"("required":["di","s"]}}})");
}

TEST_CASE(combo_multi_map) {
    const auto result = json::schema_string<multi_map>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("a":{"type":"object",)" R"("additionalProperties":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("b":{"type":"object",)" R"("additionalProperties":{"type":"string"}}},)" R"("required":["a","b"]})");
}

TEST_CASE(combo_many_fields) {
    const auto result = json::schema_string<many_fields>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("a":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("b":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("c":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("d":{"type":"string"},)" R"("e":{"type":"boolean"},)" R"("f":{"type":"number"}},)" R"("required":["a","b","c","d","e","f"]})");
}

TEST_CASE(combo_set_of_struct) {
    const auto result = json::schema_string<set_of_struct>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("ids":{"type":"array",)" R"("items":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("uniqueItems":true},)" R"("name":{"type":"string"}},)" R"("required":["ids","name"]})");
}

TEST_CASE(combo_trivial_nested) {
    const auto result = json::schema_string<trivial_nested>().value();
    EXPECT_EQ(result, R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("p":{)" R"("$ref":"#/$defs/point2d"},)" R"("z":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["p","z"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

// ---------------------------------------------------------------------------
// Self-referential struct
// ---------------------------------------------------------------------------

TEST_CASE(self_referential_struct) {
    static struct_type_info self_info = {
        {type_kind::structure, "self_ref"},
        false,
        false,
        {},
    };
    const static optional_type_info opt_self = {
        {type_kind::optional, "optional<self_ref>"},
        []() -> const type_info& { return self_info; },
    };
    const static field_info self_fields[] = {
        {"value", {}, 0, 0, type_info_of<std::int32_t>, false, false, false, false},
        {"next",
         {},
         0,              1,
         []() -> const type_info& { return opt_self; },
         false,                                                false,
         false,                                                              false},
    };
    self_info.fields = {self_fields, 2};

    const auto result = json::schema_string(self_info).value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("value":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)" R"("next":{"oneOf":[{"$ref":"#"},{"type":"null"}]}},)" R"("required":["value"]})");
}

// ---------------------------------------------------------------------------
// Empty enum
// ---------------------------------------------------------------------------

TEST_CASE(empty_enum) {
    const static enum_type_info empty_ei = {
        {type_kind::enumeration, "empty_enum"},
        {},
        nullptr,
        type_kind::int32,
    };
    const auto result = json::schema_string(empty_ei).value();
    EXPECT_EQ(result, R"({"$schema":"https://json-schema.org/draft/2020-12/schema","enum":[]})");
}

// ---------------------------------------------------------------------------
// Variant nesting variant
// ---------------------------------------------------------------------------

TEST_CASE(variant_of_variant) {
    const static type_info_fn inner_alts[] = {
        type_info_of<std::string>,
        type_info_of<bool>,
    };
    const static variant_type_info inner_var = {
        {type_kind::variant, "inner_variant"},
        {inner_alts, 2},
        tag_mode::none,
        {},
        {},
        {},
    };

    const static type_info_fn outer_alts[] = {
        type_info_of<std::int32_t>,
        []() -> const type_info& { return inner_var; },
    };
    const static variant_type_info outer_var = {
        {type_kind::variant, "outer_variant"},
        {outer_alts, 2},
        tag_mode::none,
        {},
        {},
        {},
    };

    const auto result = json::schema_string(outer_var).value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("oneOf":[)" R"({"type":"integer","minimum":-2147483648,"maximum":2147483647},)" R"({"oneOf":[)" R"({"type":"string"},)" R"({"type":"boolean"}]}]})");
}

// ---------------------------------------------------------------------------
// Field ordering stability
// ---------------------------------------------------------------------------

TEST_CASE(field_ordering_stability) {
    const static field_info ordered_fields[] = {
        {"zebra",  {}, 0, 0, type_info_of<std::string>,  false, false, false, false},
        {"alpha",  {}, 0, 1, type_info_of<std::int32_t>, false, false, false, false},
        {"middle", {}, 0, 2, type_info_of<bool>,         false, false, false, false},
        {"beta",   {}, 0, 3, type_info_of<double>,       false, false, false, false},
    };
    const static struct_type_info ordered_info = {
        {type_kind::structure, "ordered_struct"},
        false,
        false,
        {ordered_fields,       4               },
    };
    const auto result = json::schema_string(ordered_info).value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("zebra":{"type":"string"},)" R"("alpha":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)" R"("middle":{"type":"boolean"},)" R"("beta":{"type":"number"}},)" R"("required":["zebra","alpha","middle","beta"]})");
}

// ---------------------------------------------------------------------------
// Variant with monostate
// ---------------------------------------------------------------------------

struct with_monostate {
    std::variant<std::monostate, std::int32_t, std::string> v;
};

TEST_CASE(variant_with_monostate) {
    const auto result = json::schema_string<with_monostate>().value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"oneOf":[)" R"({"type":"null"},)" R"({"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"string"}]}},)" R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Mutual recursion
// ---------------------------------------------------------------------------

TEST_CASE(mutual_recursion) {
    static struct_type_info info_a = {
        {type_kind::structure, "node_a"},
        false,
        false,
        {}
    };
    static struct_type_info info_b = {
        {type_kind::structure, "node_b"},
        false,
        false,
        {}
    };

    const static optional_type_info opt_b = {
        {type_kind::optional, "optional<node_b>"},
        []() -> const type_info& { return info_b; },
    };
    const static optional_type_info opt_a = {
        {type_kind::optional, "optional<node_a>"},
        []() -> const type_info& { return info_a; },
    };

    const static field_info fields_a[] = {
        {"value", {}, 0, 0, type_info_of<std::int32_t>,                 false, false, false, false},
        {"b",     {}, 0, 1, []() -> const type_info& { return opt_b; }, false, false, false, false},
    };
    const static field_info fields_b[] = {
        {"name", {}, 0, 0, type_info_of<std::string>,                  false, false, false, false},
        {"a",    {}, 0, 1, []() -> const type_info& { return opt_a; }, false, false, false, false},
    };
    info_a.fields = {fields_a, 2};
    info_b.fields = {fields_b, 2};

    const auto result = json::schema_string(info_a).value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("value":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)" R"("b":{"oneOf":[{"$ref":"#/$defs/node_b"},{"type":"null"}]}},)" R"("required":["value"],)" R"("$defs":{)" R"("node_b":{"type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("a":{"oneOf":[{"$ref":"#"},{"type":"null"}]}},)" R"("required":["name"]}}})");
}

// ---------------------------------------------------------------------------
// Bytes type
// ---------------------------------------------------------------------------

TEST_CASE(bytes_field) {
    const static type_info bytes_ti = {type_kind::bytes, "bytes"};
    const static field_info bytes_fields[] = {
        {"data",
         {},
         0, 0,
         []() -> const type_info& { return bytes_ti; },
         false, false,
         false, false},
    };
    const static struct_type_info bytes_struct = {
        {type_kind::structure, "with_bytes"},
        false,
        false,
        {bytes_fields,         1           },
    };
    const auto result = json::schema_string(bytes_struct).value();
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("data":{"type":"string"}},)" R"("required":["data"]})");
}

};  // TEST_SUITE(serde_json_schema)

}  // namespace

}  // namespace kota::meta
