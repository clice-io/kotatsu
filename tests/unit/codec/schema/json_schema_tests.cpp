// ---------------------------------------------------------------------------
// json_schema_tests.cpp
//
// Unit tests for the JSON Schema codegen module. Each test renders a C++ type
// through virtual_schema::type_info_of<T>() and json_schema::render(), then
// compares the output against the exact expected JSON string.
//
// Test groups:
//   1.  Root scalars (bool, int types, float, double, char, string)
//   2.  Scalar struct wrappers (single-field objects)
//   3.  Root enums (enum class rendered as {"enum":[...]})
//   4.  Containers (vector, set, map, nested containers)
//   5.  Tuple / Pair (prefixItems)
//   6.  Basic structs (empty, single-field, multi-field)
//   7.  Nested structs ($ref / $defs)
//   8.  Optional / pointer (not required)
//   9.  default_value attribute (not required)
//   10. deny_unknown_fields (additionalProperties:false)
//   11. skip attribute (omitted from schema)
//   12. flatten attribute (inline inner struct fields)
//   13. rename attribute (custom property name)
//   14. Variant tag_mode::none (untagged oneOf)
//   15. Variant tag_mode::external (externally tagged)
//   16. Variant tag_mode::internal (internally tagged)
//   17. Variant tag_mode::adjacent (adjacently tagged)
//   18. More containers (maps of structs, deep nesting, etc.)
//   19. Struct with pointer to struct ($ref + not required)
//   20. $defs dedup (same type referenced multiple times)
//   21. Struct with enum fields
//   22. Nested struct with optional
//   23. Variant in container
//   24. Combinations (complex real-world-like types)
// ---------------------------------------------------------------------------

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/codec/schema/json_schema.h"

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

namespace js = kota::codec::schema::json_schema;

// ===========================================================================
TEST_SUITE(serde_json_schema) {

// ---------------------------------------------------------------------------
// Group 1: Root scalars (13 tests)
//
// Each primitive C++ type is rendered as a standalone JSON Schema document.
// Integers include minimum/maximum bounds matching the C++ type range.
// ---------------------------------------------------------------------------

TEST_CASE(root_bool) {
    const auto result = js::render(type_info_of<bool>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"boolean"})");
}

TEST_CASE(root_i8) {
    const auto result = js::render(type_info_of<std::int8_t>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"integer",)" R"("minimum":-128,)" R"("maximum":127})");
}

TEST_CASE(root_i16) {
    const auto result = js::render(type_info_of<std::int16_t>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"integer",)" R"("minimum":-32768,)" R"("maximum":32767})");
}

TEST_CASE(root_i32) {
    const auto result = js::render(type_info_of<std::int32_t>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647})");
}

TEST_CASE(root_i64) {
    const auto result = js::render(type_info_of<std::int64_t>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"integer",)" R"("minimum":-9223372036854775808,)" R"("maximum":9223372036854775807})");
}

TEST_CASE(root_u8) {
    const auto result = js::render(type_info_of<std::uint8_t>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"integer",)" R"("minimum":0,)" R"("maximum":255})");
}

TEST_CASE(root_u16) {
    const auto result = js::render(type_info_of<std::uint16_t>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"integer",)" R"("minimum":0,)" R"("maximum":65535})");
}

TEST_CASE(root_u32) {
    const auto result = js::render(type_info_of<std::uint32_t>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"integer",)" R"("minimum":0,)" R"("maximum":4294967295})");
}

TEST_CASE(root_u64) {
    const auto result = js::render(type_info_of<std::uint64_t>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"integer",)" R"("minimum":0,)" R"("maximum":18446744073709551615})");
}

TEST_CASE(root_f32) {
    const auto result = js::render(type_info_of<float>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"number"})");
}

TEST_CASE(root_f64) {
    const auto result = js::render(type_info_of<double>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"number"})");
}

TEST_CASE(root_char) {
    const auto result = js::render(type_info_of<char>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"string"})");
}

TEST_CASE(root_string) {
    const auto result = js::render(type_info_of<std::string>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"string"})");
}

// ---------------------------------------------------------------------------
// Group 2: Scalar struct wrappers (13 tests)
//
// Each struct wraps a single scalar field named "v". The schema should be
// an object with one required property whose type matches the scalar.
// ---------------------------------------------------------------------------

TEST_CASE(scalar_wrapper_bool) {
    const auto result = js::render(type_info_of<s_bool>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"boolean"}},)" R"("required":["v"]})");
}

TEST_CASE(scalar_wrapper_i8) {
    const auto result = js::render(type_info_of<s_i8>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"integer",)" R"("minimum":-128,)" R"("maximum":127}},)" R"("required":["v"]})");
}

TEST_CASE(scalar_wrapper_i16) {
    const auto result = js::render(type_info_of<s_i16>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"integer",)" R"("minimum":-32768,)" R"("maximum":32767}},)" R"("required":["v"]})");
}

TEST_CASE(scalar_wrapper_i32) {
    const auto result = js::render(type_info_of<s_i32>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["v"]})");
}

TEST_CASE(scalar_wrapper_i64) {
    const auto result = js::render(type_info_of<s_i64>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"integer",)" R"("minimum":-9223372036854775808,)" R"("maximum":9223372036854775807}},)" R"("required":["v"]})");
}

TEST_CASE(scalar_wrapper_u8) {
    const auto result = js::render(type_info_of<s_u8>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"integer",)" R"("minimum":0,)" R"("maximum":255}},)" R"("required":["v"]})");
}

TEST_CASE(scalar_wrapper_u16) {
    const auto result = js::render(type_info_of<s_u16>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"integer",)" R"("minimum":0,)" R"("maximum":65535}},)" R"("required":["v"]})");
}

TEST_CASE(scalar_wrapper_u32) {
    const auto result = js::render(type_info_of<s_u32>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"integer",)" R"("minimum":0,)" R"("maximum":4294967295}},)" R"("required":["v"]})");
}

TEST_CASE(scalar_wrapper_u64) {
    const auto result = js::render(type_info_of<s_u64>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"integer",)" R"("minimum":0,)" R"("maximum":18446744073709551615}},)" R"("required":["v"]})");
}

TEST_CASE(scalar_wrapper_f32) {
    const auto result = js::render(type_info_of<s_f32>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"number"}},)" R"("required":["v"]})");
}

TEST_CASE(scalar_wrapper_f64) {
    const auto result = js::render(type_info_of<s_f64>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"number"}},)" R"("required":["v"]})");
}

TEST_CASE(scalar_wrapper_char) {
    const auto result = js::render(type_info_of<s_char>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"string"}},)" R"("required":["v"]})");
}

TEST_CASE(scalar_wrapper_str) {
    const auto result = js::render(type_info_of<s_str>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"string"}},)" R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Group 3: Root enums (5 tests)
//
// Enums are rendered as {"enum":["variant1","variant2",...]} using the
// enumerator names as string values.
// ---------------------------------------------------------------------------

TEST_CASE(root_enum_color_i8) {
    const auto result = js::render(type_info_of<color_i8>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("enum":["red","green","blue"]})");
}

TEST_CASE(root_enum_single) {
    const auto result = js::render(type_info_of<single_enum>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("enum":["only"]})");
}

TEST_CASE(root_enum_status) {
    const auto result = js::render(type_info_of<status>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("enum":["ok","fail","pending"]})");
}

TEST_CASE(root_enum_flag_u8) {
    const auto result = js::render(type_info_of<flag_u8>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("enum":["off","on"]})");
}

TEST_CASE(root_enum_level_i16) {
    const auto result = js::render(type_info_of<level_i16>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("enum":["low","mid","high"]})");
}

// ---------------------------------------------------------------------------
// Group 4: Containers (5 tests)
//
// std::vector maps to {"type":"array","items":{...}},
// std::set adds "uniqueItems":true,
// std::map maps to {"type":"object","additionalProperties":{...}}.
// ---------------------------------------------------------------------------

TEST_CASE(container_vec_i32) {
    const auto result = js::render(type_info_of<s_vec_i32>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"array",)" R"("items":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}},)" R"("required":["v"]})");
}

TEST_CASE(container_set_i32) {
    const auto result = js::render(type_info_of<s_set_i32>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"array",)" R"("items":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("uniqueItems":true}},)" R"("required":["v"]})");
}

TEST_CASE(container_map_str_i32) {
    const auto result = js::render(type_info_of<s_map_str_i32>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"object",)" R"("additionalProperties":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}},)" R"("required":["v"]})");
}

TEST_CASE(container_vec_vec_i32) {
    const auto result = js::render(type_info_of<s_vec_vec_i32>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"array",)" R"("items":{"type":"array",)" R"("items":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}}},)" R"("required":["v"]})");
}

TEST_CASE(container_map_str_vec_i32) {
    const auto result = js::render(type_info_of<s_map_str_vec_i32>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"object",)" R"("additionalProperties":{"type":"array",)" R"("items":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}}},)" R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Group 5: Tuple / Pair (4 tests)
//
// std::pair and std::tuple map to {"type":"array","prefixItems":[...]}.
// ---------------------------------------------------------------------------

TEST_CASE(tuple_pair) {
    const auto result = js::render(type_info_of<s_pair>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"array",)" R"("prefixItems":[)" R"({"type":"string"},)" R"({"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}]}},)" R"("required":["v"]})");
}

TEST_CASE(tuple_triple) {
    const auto result = js::render(type_info_of<s_tuple>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"array",)" R"("prefixItems":[)" R"({"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"string"},)" R"({"type":"boolean"}]}},)" R"("required":["v"]})");
}

TEST_CASE(tuple_pair_in_struct) {
    const auto result = js::render(type_info_of<with_pair_field>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("p":{"type":"array",)" R"("prefixItems":[)" R"({"type":"string"},)" R"({"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}]},)" R"("name":{"type":"string"}},)" R"("required":["p","name"]})");
}

TEST_CASE(tuple_in_struct) {
    const auto result = js::render(type_info_of<with_tuple_field>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("t":{"type":"array",)" R"("prefixItems":[)" R"({"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"boolean"}]},)" R"("name":{"type":"string"}},)" R"("required":["t","name"]})");
}

// ---------------------------------------------------------------------------
// Group 6: Basic structs (4 tests)
//
// Structs map to {"type":"object","properties":{...},"required":[...]}.
// Empty structs have no required array. All non-optional fields are required.
// ---------------------------------------------------------------------------

TEST_CASE(struct_empty) {
    const auto result = js::render(type_info_of<empty_struct>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{}})");
}

TEST_CASE(struct_single_field) {
    const auto result = js::render(type_info_of<single_field>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x"]})");
}

TEST_CASE(struct_point2d) {
    const auto result = js::render(type_info_of<point2d>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]})");
}

TEST_CASE(struct_with_string) {
    const auto result = js::render(type_info_of<with_string>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("value":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["name","value"]})");
}

// ---------------------------------------------------------------------------
// Group 7: Nested structs (4 tests)
//
// When a struct field references another struct, the referenced struct is
// emitted in "$defs" and the field uses "$ref" to point to it.
// ---------------------------------------------------------------------------

TEST_CASE(nested_inner) {
    const auto result = js::render(type_info_of<inner>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("a":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["a"]})");
}

TEST_CASE(nested_middle) {
    const auto result = js::render(type_info_of<middle>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("i":{"$ref":"#/$defs/inner"},)" R"("s":{"type":"string"}},)" R"("required":["i","s"],)" R"("$defs":{)" R"("inner":{"type":"object",)" R"("properties":{)" R"("a":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["a"]}}})");
}

TEST_CASE(nested_outer) {
    const auto result = js::render(type_info_of<outer>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("m":{"$ref":"#/$defs/middle"},)" R"("n":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["m","n"],)" R"("$defs":{)" R"("inner":{"type":"object",)" R"("properties":{)" R"("a":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["a"]},)" R"("middle":{"type":"object",)" R"("properties":{)" R"("i":{"$ref":"#/$defs/inner"},)" R"("s":{"type":"string"}},)" R"("required":["i","s"]}}})");
}

TEST_CASE(nested_with_enum) {
    const auto result = js::render(type_info_of<with_enum>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("c":{"enum":["red","green","blue"]},)" R"("name":{"type":"string"}},)" R"("required":["c","name"]})");
}

// ---------------------------------------------------------------------------
// Group 8: Optional / pointer (5 tests)
//
// std::optional, std::unique_ptr, and std::shared_ptr fields are not
// included in the "required" array but still appear in "properties".
// ---------------------------------------------------------------------------

TEST_CASE(optional_field) {
    const auto result = js::render(type_info_of<with_optional>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("age":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["name"]})");
}

TEST_CASE(unique_ptr_field) {
    const auto result = js::render(type_info_of<with_unique>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("ptr":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["name"]})");
}

TEST_CASE(shared_ptr_field) {
    const auto result = js::render(type_info_of<with_shared>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("ptr":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["name"]})");
}

TEST_CASE(all_optional_fields) {
    const auto result = js::render(type_info_of<all_optional>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("a":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("b":{"type":"string"}}})");
}

TEST_CASE(all_ptr_types) {
    const auto result = js::render(type_info_of<with_all_ptr>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("opt":{"type":"string"},)" R"("uniq":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("shr":{"type":"boolean"}}})");
}

// ---------------------------------------------------------------------------
// Group 9: default_value attribute (2 tests)
//
// Fields annotated with attrs::default_value are not required.
// ---------------------------------------------------------------------------

TEST_CASE(attr_default_value) {
    const auto result = js::render(type_info_of<with_default>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("count":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["name"]})");
}

TEST_CASE(all_default_fields) {
    const auto result = js::render(type_info_of<all_default>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"string"}}})");
}

// ---------------------------------------------------------------------------
// Group 10: deny_unknown_fields (1 test)
//
// When deny_unknown is true, "additionalProperties":false is emitted.
// This test uses manually constructed type_info.
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
    const auto result = js::render(deny_info);
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("count":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["name","count"],)" R"("additionalProperties":false})");
}

// ---------------------------------------------------------------------------
// Group 11: skip (2 tests)
//
// Fields annotated with attrs::skip are omitted from the schema entirely.
// ---------------------------------------------------------------------------

TEST_CASE(attr_skip) {
    const auto result = js::render(type_info_of<with_skip>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("visible":{"type":"string"}},)" R"("required":["visible"]})");
}

TEST_CASE(skip_and_default) {
    const auto result = js::render(type_info_of<skip_default>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("count":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["name"]})");
}

// ---------------------------------------------------------------------------
// Group 12: flatten (3 tests)
//
// Fields annotated with attrs::flatten have their inner struct's fields
// inlined into the parent, preserving required/optional status and renames.
// ---------------------------------------------------------------------------

TEST_CASE(attr_flatten) {
    const auto result = js::render(type_info_of<with_flatten>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("a":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("b":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("extra":{"type":"string"}},)" R"("required":["a","b","extra"]})");
}

TEST_CASE(flatten_with_optional) {
    const auto result = js::render(type_info_of<flatten_opt>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("tag":{"type":"string"}},)" R"("required":["x","tag"]})");
}

TEST_CASE(flatten_with_rename) {
    const auto result = js::render(type_info_of<flatten_rename>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("alpha":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("b":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("extra":{"type":"string"}},)" R"("required":["alpha","b","extra"]})");
}

// ---------------------------------------------------------------------------
// Group 13: rename (1 test)
//
// Fields annotated with attrs::rename<"name"> use the rename as the
// property key instead of the C++ field name.
// ---------------------------------------------------------------------------

TEST_CASE(attr_rename) {
    const auto result = js::render(type_info_of<with_rename>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("my_field":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"string"}},)" R"("required":["my_field","y"]})");
}

// ---------------------------------------------------------------------------
// Group 14: Variant (tag_mode::none) (2 tests)
//
// Untagged variants use "oneOf" with each alternative's schema directly.
// ---------------------------------------------------------------------------

TEST_CASE(variant_untagged) {
    const auto result = js::render(type_info_of<var_none>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"oneOf":[)" R"({"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"string"}]}},)" R"("required":["v"]})");
}

TEST_CASE(variant_three_alts) {
    const auto result = js::render(type_info_of<var_three>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"oneOf":[)" R"({"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"string"},)" R"({"type":"boolean"}]}},)" R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Group 15: Variant (tag_mode::external) (1 test)
//
// External-tagged variants use "oneOf" where each alternative is an object
// with a single property (the variant name) and additionalProperties:false.
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
    const auto result = js::render(ext_wrap);
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"oneOf":[)" R"({"type":"object",)" R"("properties":{)" R"("num":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["num"],)" R"("additionalProperties":false},)" R"({"type":"object",)" R"("properties":{)" R"("text":{"type":"string"}},)" R"("required":["text"],)" R"("additionalProperties":false}]}},)" R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Group 16: Variant (tag_mode::internal) (1 test)
//
// Internal-tagged variants use "oneOf" with "allOf" combining "$ref" to
// the struct definition and a discriminator property with a "const" value.
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
    const auto result = js::render(int_wrap);
    EXPECT_EQ(result,
              R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"oneOf":[)" R"({"allOf":[)" R"({"$ref":"#/$defs/point2d"},)" R"({"properties":{)" R"("type":{"const":"point"}},)" R"("required":["type"]}]},)" R"({"allOf":[)" R"({"$ref":"#/$defs/inner"},)" R"({"properties":{)" R"("type":{"const":"inner"}},)" R"("required":["type"]}]}]}},)" R"("required":["v"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]},)" R"("inner":{"type":"object",)" R"("properties":{)" R"("a":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["a"]}}})");
}

// ---------------------------------------------------------------------------
// Group 17: Variant (tag_mode::adjacent) (1 test)
//
// Adjacent-tagged variants use "oneOf" where each alternative is an object
// with a tag field (const) and a content field, plus additionalProperties.
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
    const auto result = js::render(adj_wrap);
    EXPECT_EQ(result, R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"oneOf":[)" R"({"type":"object",)" R"("properties":{)" R"("t":{"const":"num"},)" R"("c":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["t","c"],)" R"("additionalProperties":false},)" R"({"type":"object",)" R"("properties":{)" R"("t":{"const":"text"},)" R"("c":{"type":"string"}},)" R"("required":["t","c"],)" R"("additionalProperties":false}]}},)" R"("required":["v"]})");
}

TEST_CASE(root_external_tagged_variant_via_public_api) {
    const auto result = js::render(type_info_of<root_external_variant>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("oneOf":[)" R"({"type":"object",)" R"("properties":{)" R"("integer":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["integer"],)" R"("additionalProperties":false},)" R"({"type":"object",)" R"("properties":{)" R"("text":{"type":"string"}},)" R"("required":["text"],)" R"("additionalProperties":false}]})");
}

TEST_CASE(root_internal_tagged_variant_via_public_api) {
    const auto result = js::render(type_info_of<root_internal_variant>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("oneOf":[)" R"({"allOf":[)" R"({"$ref":"#/$defs/tagged_circle"},)" R"({"properties":{)" R"("kind":{"const":"circle"}},)" R"("required":["kind"]}]},)" R"({"allOf":[)" R"({"$ref":"#/$defs/tagged_rect"},)" R"({"properties":{)" R"("kind":{"const":"rect"}},)" R"("required":["kind"]}]}],)" R"("$defs":{)" R"("tagged_circle":{"type":"object",)" R"("properties":{)" R"("radius":{"type":"number"}},)" R"("required":["radius"]},)" R"("tagged_rect":{"type":"object",)" R"("properties":{)" R"("width":{"type":"number"},)" R"("height":{"type":"number"}},)" R"("required":["width","height"]}}})");
}

TEST_CASE(root_adjacent_tagged_variant_via_public_api) {
    const auto result = js::render(type_info_of<root_adjacent_variant>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("oneOf":[)" R"({"type":"object",)" R"("properties":{)" R"("type":{"const":"integer"},)" R"("value":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["type","value"],)" R"("additionalProperties":false},)" R"({"type":"object",)" R"("properties":{)" R"("type":{"const":"text"},)" R"("value":{"type":"string"}},)" R"("required":["type","value"],)" R"("additionalProperties":false}]})");
}

TEST_CASE(opaque_root_renders_valid_json) {
    const auto result = js::render(type_info_of<json_schema_opaque_root>());
    EXPECT_EQ(result, R"({"$schema":"https://json-schema.org/draft/2020-12/schema"})");
}

// ---------------------------------------------------------------------------
// Group 18: More containers (10 tests)
//
// Additional container patterns: maps of structs/enums, vectors of optionals,
// optional vectors, nested maps, deep containers, etc.
// ---------------------------------------------------------------------------

TEST_CASE(map_str_struct) {
    const auto result = js::render(type_info_of<map_str_struct>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("entries":{"type":"object",)" R"("additionalProperties":{)" R"("$ref":"#/$defs/point2d"}}},)" R"("required":["entries"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

TEST_CASE(map_str_enum) {
    const auto result = js::render(type_info_of<map_str_enum>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("entries":{"type":"object",)" R"("additionalProperties":{)" R"("enum":["red","green","blue"]}}},)" R"("required":["entries"]})");
}

TEST_CASE(vec_optional_items) {
    const auto result = js::render(type_info_of<vec_optional>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"array",)" R"("items":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}},)" R"("required":["v"]})");
}

TEST_CASE(optional_vec_field) {
    const auto result = js::render(type_info_of<optional_vec>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("v":{"type":"array",)" R"("items":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}}})");
}

TEST_CASE(vec_of_enum) {
    const auto result = js::render(type_info_of<vec_enum>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("colors":{"type":"array",)" R"("items":{)" R"("enum":["red","green","blue"]}}},)" R"("required":["colors"]})");
}

TEST_CASE(set_of_string) {
    const auto result = js::render(type_info_of<set_string>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("tags":{"type":"array",)" R"("items":{"type":"string"},)" R"("uniqueItems":true}},)" R"("required":["tags"]})");
}

TEST_CASE(vec_of_map) {
    const auto result = js::render(type_info_of<vec_map>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("items":{"type":"array",)" R"("items":{"type":"object",)" R"("additionalProperties":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}}},)" R"("required":["items"]})");
}

TEST_CASE(map_of_vec_struct) {
    const auto result = js::render(type_info_of<map_vec_struct>());
    EXPECT_EQ(result, R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("groups":{"type":"object",)" R"("additionalProperties":{"type":"array",)" R"("items":{)" R"("$ref":"#/$defs/point2d"}}}},)" R"("required":["groups"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

TEST_CASE(deep_container_field) {
    const auto result = js::render(type_info_of<deep_container>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("data":{"type":"object",)" R"("additionalProperties":{"type":"array",)" R"("items":{"type":"object",)" R"("additionalProperties":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}}}},)" R"("required":["data"]})");
}

TEST_CASE(map_of_map_field) {
    const auto result = js::render(type_info_of<map_of_map>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("m":{"type":"object",)" R"("additionalProperties":{"type":"object",)" R"("additionalProperties":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}}},)" R"("required":["m"]})");
}

// ---------------------------------------------------------------------------
// Group 19: Struct with pointer to struct (2 tests)
//
// When a smart pointer or optional points to a struct, the struct is emitted
// in $defs and referenced via $ref. The field is not required.
// ---------------------------------------------------------------------------

TEST_CASE(shared_ptr_to_struct) {
    const auto result = js::render(type_info_of<shared_struct>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("name":{"type":"string"},)" R"("point":{)" R"("$ref":"#/$defs/point2d"}},)" R"("required":["name"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

TEST_CASE(optional_struct_field) {
    const auto result = js::render(type_info_of<optional_struct>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("point":{)" R"("$ref":"#/$defs/point2d"},)" R"("name":{"type":"string"}},)" R"("required":["name"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

// ---------------------------------------------------------------------------
// Group 20: $defs dedup (1 test)
//
// When the same struct type is referenced multiple times (as fields, in
// arrays, etc.), the $defs entry should only appear once.
// ---------------------------------------------------------------------------

TEST_CASE(defs_dedup_multi_ref) {
    const auto result = js::render(type_info_of<multi_ref>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("a":{)" R"("$ref":"#/$defs/point2d"},)" R"("b":{)" R"("$ref":"#/$defs/point2d"},)" R"("list":{"type":"array",)" R"("items":{)" R"("$ref":"#/$defs/point2d"}}},)" R"("required":["a","b","list"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

// ---------------------------------------------------------------------------
// Group 21: Struct with enum fields (3 tests)
//
// Enum fields are inlined as {"enum":[...]} without $ref/$defs.
// ---------------------------------------------------------------------------

TEST_CASE(multi_enum_fields) {
    const auto result = js::render(type_info_of<multi_enum>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("c":{"enum":["red","green","blue"]},)" R"("s":{"enum":["ok","fail","pending"]},)" R"("label":{"type":"string"}},)" R"("required":["c","s","label"]})");
}

TEST_CASE(with_flag_enum) {
    const auto result = js::render(type_info_of<with_flag>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("f":{"enum":["off","on"]},)" R"("name":{"type":"string"}},)" R"("required":["f","name"]})");
}

TEST_CASE(with_level_enum) {
    const auto result = js::render(type_info_of<with_level>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("l":{"enum":["low","mid","high"]},)" R"("v":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["l","v"]})");
}

// ---------------------------------------------------------------------------
// Group 22: Nested struct with optional (1 test)
//
// An optional field pointing to a struct still uses $ref/$defs but is
// not included in the required array.
// ---------------------------------------------------------------------------

TEST_CASE(optional_inner_field) {
    const auto result = js::render(type_info_of<optional_inner>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("i":{)" R"("$ref":"#/$defs/inner"},)" R"("name":{"type":"string"}},)" R"("required":["name"],)" R"("$defs":{)" R"("inner":{"type":"object",)" R"("properties":{)" R"("a":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["a"]}}})");
}

// ---------------------------------------------------------------------------
// Group 23: Variant in container (1 test)
//
// A vector of variants produces an array whose items use "oneOf".
// ---------------------------------------------------------------------------

TEST_CASE(vec_of_variant) {
    const auto result = js::render(type_info_of<vec_variant>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("items":{"type":"array",)" R"("items":{"oneOf":[)" R"({"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"({"type":"string"}]}}},)" R"("required":["items"]})");
}

// ---------------------------------------------------------------------------
// Group 24: Combinations (8 tests)
//
// Structs combining multiple feature categories: enums + optional + containers,
// nested $ref structs, deep nesting, multiple maps, many fields, etc.
// ---------------------------------------------------------------------------

TEST_CASE(combo_mixed_fields) {
    const auto result = js::render(type_info_of<combo>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("color":{)" R"("enum":["red","green","blue"]},)" R"("label":{"type":"string"},)" R"("values":{"type":"array",)" R"("items":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("attrs":{"type":"object",)" R"("additionalProperties":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}}},)" R"("required":["color","values","attrs"]})");
}

TEST_CASE(combo_nested_struct_refs) {
    const auto result = js::render(type_info_of<nested_combo>());
    EXPECT_EQ(result,
              R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("point":{)" R"("$ref":"#/$defs/point2d"},)" R"("color":{)" R"("enum":["red","green","blue"]},)" R"("points":{"type":"array",)" R"("items":{)" R"("$ref":"#/$defs/point2d"}},)" R"("named_points":{"type":"object",)" R"("additionalProperties":{)" R"("$ref":"#/$defs/point2d"}}},)" R"("required":[)" R"("point","color","points","named_points"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

TEST_CASE(combo_vec_of_struct) {
    const auto result = js::render(type_info_of<vec_of_struct>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("items":{"type":"array",)" R"("items":{)" R"("$ref":"#/$defs/point2d"}}},)" R"("required":["items"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

TEST_CASE(combo_deep_nesting) {
    const auto result = js::render(type_info_of<deep_outer>());
    EXPECT_EQ(result,
              R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("dm":{)" R"("$ref":"#/$defs/deep_middle"},)" R"("n":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["dm","n"],)" R"("$defs":{)" R"("deep_inner":{"type":"object",)" R"("properties":{)" R"("c":{)" R"("enum":["red","green","blue"]},)" R"("v":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["c","v"]},)" R"("deep_middle":{"type":"object",)" R"("properties":{)" R"("di":{)" R"("$ref":"#/$defs/deep_inner"},)" R"("s":{"type":"string"}},)" R"("required":["di","s"]}}})");
}

TEST_CASE(combo_multi_map) {
    const auto result = js::render(type_info_of<multi_map>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("a":{"type":"object",)" R"("additionalProperties":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("b":{"type":"object",)" R"("additionalProperties":{"type":"string"}}},)" R"("required":["a","b"]})");
}

TEST_CASE(combo_many_fields) {
    const auto result = js::render(type_info_of<many_fields>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("a":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("b":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("c":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("d":{"type":"string"},)" R"("e":{"type":"boolean"},)" R"("f":{"type":"number"}},)" R"("required":["a","b","c","d","e","f"]})");
}

TEST_CASE(combo_set_of_struct) {
    const auto result = js::render(type_info_of<set_of_struct>());
    EXPECT_EQ(
        result,
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("ids":{"type":"array",)" R"("items":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("uniqueItems":true},)" R"("name":{"type":"string"}},)" R"("required":["ids","name"]})");
}

TEST_CASE(combo_trivial_nested) {
    const auto result = js::render(type_info_of<trivial_nested>());
    EXPECT_EQ(result, R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)" R"("type":"object",)" R"("properties":{)" R"("p":{)" R"("$ref":"#/$defs/point2d"},)" R"("z":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["p","z"],)" R"("$defs":{)" R"("point2d":{"type":"object",)" R"("properties":{)" R"("x":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647},)" R"("y":{"type":"integer",)" R"("minimum":-2147483648,)" R"("maximum":2147483647}},)" R"("required":["x","y"]}}})");
}

};  // TEST_SUITE(serde_json_schema)

}  // namespace

}  // namespace kota::meta
