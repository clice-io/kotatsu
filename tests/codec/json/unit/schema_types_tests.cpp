#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/configs.h"
#include "codec/json/harness/schema.h"
#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/codec/json/schema.h"
#include "kota/codec/macro.h"

namespace kota::meta {

struct json_schema_opaque_root {};

}  // namespace kota::meta

namespace kota::meta {

template <>
constexpr inline bool schema_opaque<kota::meta::json_schema_opaque_root> = true;

}  // namespace kota::meta

namespace kota::meta {

namespace {

using test::color_i8;
using test::point2d;
using test::inner;
using test::root_external_variant;

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

KOTATSU_ANNOTATION(root_internal_annotation, tag = "kind", tag_names = {"circle", "rect"});
using root_internal_variant =
    annotate<root_internal_annotation>::type<std::variant<tagged_circle, tagged_rect>>;

KOTATSU_ANNOTATION(root_adjacent_annotation,
                   tag = "type",
                   content = "value",
                   tag_names = {"integer", "text"});
using root_adjacent_variant =
    annotate<root_adjacent_annotation>::type<std::variant<std::int32_t, std::string>>;

// ---------------------------------------------------------------------------
// Additional types
// ---------------------------------------------------------------------------
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

struct vec_enum {
    std::vector<color_i8> colors;
};

struct set_string {
    std::set<std::string> tags;
};

struct vec_map {
    std::vector<std::map<std::string, std::int32_t>> items;
};

struct map_vec_struct {
    std::map<std::string, std::vector<point2d>> groups;
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

struct deep_container {
    std::map<std::string, std::vector<std::map<std::string, std::int32_t>>> data;
};

struct map_of_map {
    std::map<std::string, std::map<std::string, std::int32_t>> m;
};

struct vec_variant {
    std::vector<std::variant<std::int32_t, std::string>> items;
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
    ZEXPECT(result == expected);
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
    ZEXPECT(result == expected);
}

ZEST_SUITE(codec_json_schema_types) {

// ---------------------------------------------------------------------------
// Root scalars
// ---------------------------------------------------------------------------

ZEST_CASE(root_bool) {
    const auto result = json::schema_string<bool>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"boolean"})");
}

ZEST_CASE(root_integers) {
    check_root_integer_schema<std::int8_t>();
    check_root_integer_schema<std::int16_t>();
    check_root_integer_schema<std::int32_t>();
    check_root_integer_schema<std::int64_t>();
    check_root_integer_schema<std::uint8_t>();
    check_root_integer_schema<std::uint16_t>();
    check_root_integer_schema<std::uint32_t>();
    check_root_integer_schema<std::uint64_t>();
}

ZEST_CASE(root_floats) {
    // The default nan_repr (Passthrough) hands non-finite values to the
    // writer, which emits null — so even the default schema admits null.
    const auto schema =
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema","anyOf":[{"type":"number"},{"type":"null"}]})";
    ZEXPECT(json::schema_string<float>().value() == schema);
    ZEXPECT(json::schema_string<double>().value() == schema);
}

ZEST_CASE(root_char) {
    const auto result = json::schema_string<char>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"string","pattern":"^[\\u0000-\\u00FF]$"})");
}

ZEST_CASE(root_string) {
    const auto result = json::schema_string<std::string>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"string"})");
}

// ---------------------------------------------------------------------------
// Scalar struct wrappers
// ---------------------------------------------------------------------------

ZEST_CASE(scalar_wrapper_bool) {
    const auto result = json::schema_string<s_bool>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"type":"boolean"}},)"
                      R"("required":["v"]})");
}

ZEST_CASE(scalar_wrapper_integers) {
    check_wrapper_integer_schema<s_i8, std::int8_t>();
    check_wrapper_integer_schema<s_i16, std::int16_t>();
    check_wrapper_integer_schema<s_i32, std::int32_t>();
    check_wrapper_integer_schema<s_i64, std::int64_t>();
    check_wrapper_integer_schema<s_u8, std::uint8_t>();
    check_wrapper_integer_schema<s_u16, std::uint16_t>();
    check_wrapper_integer_schema<s_u32, std::uint32_t>();
    check_wrapper_integer_schema<s_u64, std::uint64_t>();
}

ZEST_CASE(scalar_wrapper_floats) {
    const auto schema =
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","properties":{"v":{"anyOf":[{"type":"number"},{"type":"null"}]}},"required":["v"]})";
    ZEXPECT(json::schema_string<s_f32>().value() == schema);
    ZEXPECT(json::schema_string<s_f64>().value() == schema);
}

ZEST_CASE(scalar_wrapper_char) {
    const auto result = json::schema_string<s_char>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"type":"string","pattern":"^[\\u0000-\\u00FF]$"}},)"
                      R"("required":["v"]})");
}

ZEST_CASE(scalar_wrapper_str) {
    const auto result = json::schema_string<s_str>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"type":"string"}},)"
                      R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Root enums
// ---------------------------------------------------------------------------

ZEST_CASE(root_enum_color_i8) {
    const auto result = json::schema_string<color_i8>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"integer","minimum":-128,"maximum":127})");
}

ZEST_CASE(root_enum_single) {
    const auto result = json::schema_string<single_enum>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"integer","minimum":-2147483648,"maximum":2147483647})");
}

ZEST_CASE(root_enum_status) {
    const auto result = json::schema_string<status>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"integer","minimum":-2147483648,"maximum":2147483647})");
}

ZEST_CASE(root_enum_flag_u8) {
    const auto result = json::schema_string<flag_u8>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"integer","minimum":0,"maximum":255})");
}

ZEST_CASE(root_enum_level_i16) {
    const auto result = json::schema_string<level_i16>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"integer","minimum":-32768,"maximum":32767})");
}

ZEST_CASE(root_enum_value_outside_name_scan) {
    // 65535 lies outside the [-128, 127] name-reflection scan, yet encodes
    // fine as a number — the schema must not reject it, so the numeric form
    // is the underlying integer's range rather than a reflected value list.
    enum class big_u16 : std::uint16_t { a = 0, c = 65535 };
    const auto encoded = json::to_string(big_u16::c);
    ZASSERT(encoded);
    ZEXPECT(*encoded == "65535");

    const auto result = json::schema_string<big_u16>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"integer","minimum":0,"maximum":65535})");
}

// ---------------------------------------------------------------------------
// Containers
// ---------------------------------------------------------------------------

ZEST_CASE(container_vec_i32) {
    const auto result = json::schema_string<s_vec_i32>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"type":"array",)"
                      R"("items":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}}},)"
                      R"("required":["v"]})");
}

ZEST_CASE(container_set_i32) {
    const auto result = json::schema_string<s_set_i32>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"type":"array",)"
                      R"("items":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}}},)"
                      R"("required":["v"]})");
}

ZEST_CASE(container_map_str_i32) {
    const auto result = json::schema_string<s_map_str_i32>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"type":"object",)"
                      R"("additionalProperties":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}}},)"
                      R"("required":["v"]})");
}

ZEST_CASE(container_vec_vec_i32) {
    const auto result = json::schema_string<s_vec_vec_i32>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"type":"array",)"
                      R"("items":{"type":"array",)"
                      R"("items":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}}}},)"
                      R"("required":["v"]})");
}

ZEST_CASE(map_str_vec_i32) {
    const auto result = json::schema_string<s_map_str_vec_i32>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"type":"object",)"
                      R"("additionalProperties":{"type":"array",)"
                      R"("items":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}}}},)"
                      R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Tuple / Pair
// ---------------------------------------------------------------------------

ZEST_CASE(tuple_pair) {
    const auto result = json::schema_string<s_pair>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"type":"array",)"
                      R"("prefixItems":[)"
                      R"({"type":"string"},)"
                      R"({"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}],)"
                      R"("items":false,)"
                      R"("minItems":2,"maxItems":2}},)"
                      R"("required":["v"]})");
}

ZEST_CASE(tuple_triple) {
    const auto result = json::schema_string<s_tuple>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"type":"array",)"
                      R"("prefixItems":[)"
                      R"({"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"({"type":"string"},)"
                      R"({"type":"boolean"}],)"
                      R"("items":false,)"
                      R"("minItems":3,"maxItems":3}},)"
                      R"("required":["v"]})");
}

ZEST_CASE(tuple_pair_in_struct) {
    const auto result = json::schema_string<with_pair_field>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("p":{"type":"array",)"
                      R"("prefixItems":[)"
                      R"({"type":"string"},)"
                      R"({"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}],)"
                      R"("items":false,)"
                      R"("minItems":2,"maxItems":2},)"
                      R"("name":{"type":"string"}},)"
                      R"("required":["p","name"]})");
}

ZEST_CASE(tuple_in_struct) {
    const auto result = json::schema_string<with_tuple_field>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("t":{"type":"array",)"
                      R"("prefixItems":[)"
                      R"({"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"({"type":"boolean"}],)"
                      R"("items":false,)"
                      R"("minItems":2,"maxItems":2},)"
                      R"("name":{"type":"string"}},)"
                      R"("required":["t","name"]})");
}

// ---------------------------------------------------------------------------
// Variant (tag_mode::none)
// ---------------------------------------------------------------------------

ZEST_CASE(variant_untagged) {
    const auto result = json::schema_string<var_none>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"anyOf":[)"
                      R"({"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"({"type":"string"}]}},)"
                      R"("required":["v"]})");
}

ZEST_CASE(variant_three_alts) {
    const auto result = json::schema_string<var_three>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"anyOf":[)"
                      R"({"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"({"type":"string"},)"
                      R"({"type":"boolean"}]}},)"
                      R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Variant (tag_mode::external)
// ---------------------------------------------------------------------------

ZEST_CASE(variant_external_tag) {
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
    };
    const static struct_type_info ext_wrap = {
        {type_kind::structure, "ext_wrap"},
        false,
        false,
        {&ext_field,           1         },
    };
    const auto result = json::schema_string(ext_wrap).value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"oneOf":[)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("num":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["num"],)"
                      R"("additionalProperties":false},)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("text":{"type":"string"}},)"
                      R"("required":["text"],)"
                      R"("additionalProperties":false}]}},)"
                      R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Variant (tag_mode::internal)
// ---------------------------------------------------------------------------

ZEST_CASE(variant_internal_tag) {
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
    };
    const static struct_type_info int_wrap = {
        {type_kind::structure, "int_wrap"},
        false,
        false,
        {&int_field,           1         },
    };
    const auto result = json::schema_string(int_wrap).value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"oneOf":[)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("x":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                      R"("y":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                      R"("type":{"const":"point"}},)"
                      R"("required":["x","y","type"]},)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("a":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                      R"("type":{"const":"inner"}},)"
                      R"("required":["a","type"]}]}},)"
                      R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Variant (tag_mode::adjacent)
// ---------------------------------------------------------------------------

ZEST_CASE(variant_adjacent_tag) {
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
    };
    const static struct_type_info adj_wrap = {
        {type_kind::structure, "adj_wrap"},
        false,
        false,
        {&adj_field,           1         },
    };
    const auto result = json::schema_string(adj_wrap).value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"oneOf":[)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("t":{"const":"num"},)"
                      R"("c":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["t","c"]},)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("t":{"const":"text"},)"
                      R"("c":{"type":"string"}},)"
                      R"("required":["t","c"]}]}},)"
                      R"("required":["v"]})");
}

ZEST_CASE(root_external_variant) {
    const auto result = json::schema_string<root_external_variant>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("oneOf":[)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("integer":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["integer"],)"
                      R"("additionalProperties":false},)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("text":{"type":"string"}},)"
                      R"("required":["text"],)"
                      R"("additionalProperties":false}]})");
}

ZEST_CASE(root_internal_variant) {
    const auto result = json::schema_string<root_internal_variant>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("oneOf":[)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("radius":{"anyOf":[{"type":"number"},{"type":"null"}]},)"
                      R"("kind":{"const":"circle"}},)"
                      R"("required":["radius","kind"]},)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("width":{"anyOf":[{"type":"number"},{"type":"null"}]},)"
                      R"("height":{"anyOf":[{"type":"number"},{"type":"null"}]},)"
                      R"("kind":{"const":"rect"}},)"
                      R"("required":["width","height","kind"]}]})");
}

ZEST_CASE(root_adjacent_variant) {
    // Keys beside the tag and the content are passed over, so allowed.
    const auto result = json::schema_string<root_adjacent_variant>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("oneOf":[)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("type":{"const":"integer"},)"
                      R"("value":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["type","value"]},)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("type":{"const":"text"},)"
                      R"("value":{"type":"string"}},)"
                      R"("required":["type","value"]}]})");
}

ZEST_CASE(root_adjacent_variant_denies_other_keys) {
    // ...unless unknown fields are denied, as the decoder then does.
    const auto result = json::schema_string<root_adjacent_variant, test::StrictConfig>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("oneOf":[)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("type":{"const":"integer"},)"
                      R"("value":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["type","value"],)"
                      R"("additionalProperties":false},)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("type":{"const":"text"},)"
                      R"("value":{"type":"string"}},)"
                      R"("required":["type","value"],)"
                      R"("additionalProperties":false}]})");
}

ZEST_CASE(opaque_root_returns_error) {
    const auto result = json::schema_string<json_schema_opaque_root>();
    ZEXPECT(!result);
}

ZEST_CASE(any_type_root) {
    const static type_info any_ti = {type_kind::any, "any"};
    const auto result = json::schema_string(any_ti).value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema"})");
}

ZEST_CASE(any_type_field) {
    const static type_info any_ti = {type_kind::any, "any"};
    const static field_info any_fields[] = {
        {"data", {}, 0, 0, []() -> const type_info& { return any_ti; }, false, false, false},
    };
    const static struct_type_info any_struct = {
        {type_kind::structure, "with_any"},
        false,
        false,
        {any_fields,           1         },
    };
    const auto result = json::schema_string(any_struct).value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{"data":{}},)"
                      R"("required":["data"]})");
}

// ---------------------------------------------------------------------------
// More containers
// ---------------------------------------------------------------------------

ZEST_CASE(map_str_struct) {
    const auto result = json::schema_string<map_str_struct>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("entries":{"type":"object",)"
                      R"("additionalProperties":{)"
                      R"("$ref":"#/$defs/point2d"}}},)"
                      R"("required":["entries"],)"
                      R"("$defs":{)"
                      R"("point2d":{"type":"object",)"
                      R"("properties":{)"
                      R"("x":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"("y":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["x","y"]}}})");
}

ZEST_CASE(map_str_enum) {
    const auto result = json::schema_string<map_str_enum>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("entries":{"type":"object",)"
                      R"("additionalProperties":{)"
                      R"("type":"integer","minimum":-128,"maximum":127}}},)"
                      R"("required":["entries"]})");
}

ZEST_CASE(vec_optional_items) {
    const auto result = json::schema_string<vec_optional>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"type":"array",)"
                      R"("items":{"anyOf":[{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"({"type":"null"}]}}},)"
                      R"("required":["v"]})");
}

ZEST_CASE(optional_vec_field) {
    const auto result = json::schema_string<optional_vec>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"anyOf":[{"type":"array",)"
                      R"("items":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"({"type":"null"}],"default":null}}})");
}

ZEST_CASE(vec_of_enum) {
    const auto result = json::schema_string<vec_enum>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("colors":{"type":"array",)"
                      R"("items":{)"
                      R"("type":"integer","minimum":-128,"maximum":127}}},)"
                      R"("required":["colors"]})");
}

ZEST_CASE(set_of_string) {
    const auto result = json::schema_string<set_string>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("tags":{"type":"array",)"
                      R"("items":{"type":"string"}}},)"
                      R"("required":["tags"]})");
}

ZEST_CASE(vec_of_map) {
    const auto result = json::schema_string<vec_map>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("items":{"type":"array",)"
                      R"("items":{"type":"object",)"
                      R"("additionalProperties":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}}}},)"
                      R"("required":["items"]})");
}

ZEST_CASE(map_of_vec_struct) {
    const auto result = json::schema_string<map_vec_struct>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("groups":{"type":"object",)"
                      R"("additionalProperties":{"type":"array",)"
                      R"("items":{)"
                      R"("$ref":"#/$defs/point2d"}}}},)"
                      R"("required":["groups"],)"
                      R"("$defs":{)"
                      R"("point2d":{"type":"object",)"
                      R"("properties":{)"
                      R"("x":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"("y":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["x","y"]}}})");
}

ZEST_CASE(deep_container_field) {
    const auto result = json::schema_string<deep_container>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("data":{"type":"object",)"
                      R"("additionalProperties":{"type":"array",)"
                      R"("items":{"type":"object",)"
                      R"("additionalProperties":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}}}}},)"
                      R"("required":["data"]})");
}

ZEST_CASE(map_of_map_field) {
    const auto result = json::schema_string<map_of_map>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("m":{"type":"object",)"
                      R"("additionalProperties":{"type":"object",)"
                      R"("additionalProperties":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}}}},)"
                      R"("required":["m"]})");
}

// ---------------------------------------------------------------------------
// Struct with enum fields
// ---------------------------------------------------------------------------

ZEST_CASE(multi_enum_fields) {
    const auto result = json::schema_string<multi_enum>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("c":{"type":"integer","minimum":-128,"maximum":127},)"
                      R"("s":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                      R"("label":{"type":"string"}},)"
                      R"("required":["c","s","label"]})");
}

ZEST_CASE(with_flag_enum) {
    const auto result = json::schema_string<with_flag>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("f":{"type":"integer","minimum":0,"maximum":255},)"
                      R"("name":{"type":"string"}},)"
                      R"("required":["f","name"]})");
}

ZEST_CASE(with_level_enum) {
    const auto result = json::schema_string<with_level>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("l":{"type":"integer","minimum":-32768,"maximum":32767},)"
                      R"("v":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["l","v"]})");
}

// ---------------------------------------------------------------------------
// Variant in container
// ---------------------------------------------------------------------------

ZEST_CASE(vec_of_variant) {
    const auto result = json::schema_string<vec_variant>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("items":{"type":"array",)"
                      R"("items":{"anyOf":[)"
                      R"({"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"({"type":"string"}]}}},)"
                      R"("required":["items"]})");
}

// ---------------------------------------------------------------------------
// Empty enum
// ---------------------------------------------------------------------------

ZEST_CASE(empty_enum) {
    const static enum_type_info empty_ei = {
        {type_kind::enumeration, "empty_enum"},
        {},
        nullptr,
        type_kind::int32,
    };
    const auto result = json::schema_string(empty_ei).value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"integer","minimum":-2147483648,"maximum":2147483647})");
}

// ---------------------------------------------------------------------------
// Variant nesting variant
// ---------------------------------------------------------------------------

ZEST_CASE(variant_of_variant) {
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
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("anyOf":[)"
                      R"({"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                      R"({"anyOf":[)"
                      R"({"type":"string"},)"
                      R"({"type":"boolean"}]}]})");
}

// ---------------------------------------------------------------------------
// Variant with monostate
// ---------------------------------------------------------------------------

struct with_monostate {
    std::variant<std::monostate, std::int32_t, std::string> v;
};

ZEST_CASE(variant_with_monostate) {
    const auto result = json::schema_string<with_monostate>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("v":{"anyOf":[)"
                      R"({"type":"null"},)"
                      R"({"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"({"type":"string"}]}},)"
                      R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Bytes type
// ---------------------------------------------------------------------------

ZEST_CASE(bytes_field) {
    const static type_info bytes_ti = {type_kind::bytes, "bytes"};
    const static field_info bytes_fields[] = {
        {"data", {}, 0, 0, []() -> const type_info& { return bytes_ti; }, false, false, false},
    };
    const static struct_type_info bytes_struct = {
        {type_kind::structure, "with_bytes"},
        false,
        false,
        {bytes_fields,         1           },
    };
    const auto result = json::schema_string(bytes_struct).value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("data":{"type":"array",)"
                      R"("items":{"type":"integer",)"
                      R"("minimum":0,)"
                      R"("maximum":255}}},)"
                      R"("required":["data"]})");
}

};  // ZEST_SUITE(codec_json_schema_types)

}  // namespace

}  // namespace kota::meta
