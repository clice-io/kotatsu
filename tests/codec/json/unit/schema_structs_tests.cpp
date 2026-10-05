#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "codec/json/harness/schema.h"
#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/codec/json/schema.h"

namespace kota::meta {

namespace {

using test::color_i8;
using test::point2d;
using test::inner;
using test::with_enum;

struct empty_struct {};

struct single_field {
    std::int32_t x;
};

struct with_string {
    std::string name;
    std::int32_t value;
};

struct middle {
    inner i;
    std::string s;
};

struct outer {
    middle m;
    std::int32_t n;
};

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

struct all_optional {
    std::optional<std::int32_t> a;
    std::optional<std::string> b;
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

struct optional_struct {
    std::optional<point2d> point;
    std::string name;
};

struct trivial_nested {
    point2d p;
    std::int32_t z;
};

struct with_all_ptr {
    std::optional<std::string> opt;
    std::unique_ptr<std::int32_t> uniq;
    std::shared_ptr<bool> shr;
};

struct optional_inner {
    std::optional<inner> i;
    std::string name;
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

ZEST_SUITE(codec_json_schema_structs) {

ZEST_CASE(struct_empty) {
    const auto result = json::schema_string<empty_struct>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{}})");
}

ZEST_CASE(struct_single_field) {
    const auto result = json::schema_string<single_field>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("x":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["x"]})");
}

ZEST_CASE(struct_point2d) {
    const auto result = json::schema_string<point2d>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("x":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"("y":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["x","y"]})");
}

ZEST_CASE(struct_with_string) {
    const auto result = json::schema_string<with_string>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("name":{"type":"string"},)"
                      R"("value":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["name","value"]})");
}

ZEST_CASE(nested_inner) {
    const auto result = json::schema_string<inner>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("a":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["a"]})");
}

ZEST_CASE(nested_middle) {
    const auto result = json::schema_string<middle>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("i":{"$ref":"#/$defs/inner"},)"
                      R"("s":{"type":"string"}},)"
                      R"("required":["i","s"],)"
                      R"("$defs":{)"
                      R"("inner":{"type":"object",)"
                      R"("properties":{)"
                      R"("a":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["a"]}}})");
}

ZEST_CASE(nested_outer) {
    const auto result = json::schema_string<outer>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("m":{"$ref":"#/$defs/middle"},)"
                      R"("n":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["m","n"],)"
                      R"("$defs":{)"
                      R"("inner":{"type":"object",)"
                      R"("properties":{)"
                      R"("a":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["a"]},)"
                      R"("middle":{"type":"object",)"
                      R"("properties":{)"
                      R"("i":{"$ref":"#/$defs/inner"},)"
                      R"("s":{"type":"string"}},)"
                      R"("required":["i","s"]}}})");
}

ZEST_CASE(nested_with_enum) {
    const auto result = json::schema_string<with_enum>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("c":{"type":"integer","minimum":-128,"maximum":127},)"
                      R"("name":{"type":"string"}},)"
                      R"("required":["c","name"]})");
}

ZEST_CASE(optional_field) {
    const auto result = json::schema_string<with_optional>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("name":{"type":"string"},)"
                      R"("age":{"anyOf":[{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"({"type":"null"}],"default":null}},)"
                      R"("required":["name"]})");
}

ZEST_CASE(unique_ptr_field) {
    const auto result = json::schema_string<with_unique>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("name":{"type":"string"},)"
                      R"("ptr":{"anyOf":[{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"({"type":"null"}],"default":null}},)"
                      R"("required":["name"]})");
}

ZEST_CASE(shared_ptr_field) {
    const auto result = json::schema_string<with_shared>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("name":{"type":"string"},)"
                      R"("ptr":{"anyOf":[{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"({"type":"null"}],"default":null}},)"
                      R"("required":["name"]})");
}

ZEST_CASE(all_optional_fields) {
    const auto result = json::schema_string<all_optional>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("a":{"anyOf":[{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"({"type":"null"}],"default":null},)"
                      R"("b":{"anyOf":[{"type":"string"},)"
                      R"({"type":"null"}],"default":null}}})");
}

ZEST_CASE(all_ptr_types) {
    const auto result = json::schema_string<with_all_ptr>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("opt":{"anyOf":[{"type":"string"},)"
                      R"({"type":"null"}],"default":null},)"
                      R"("uniq":{"anyOf":[{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"({"type":"null"}],"default":null},)"
                      R"("shr":{"anyOf":[{"type":"boolean"},)"
                      R"({"type":"null"}],"default":null}}})");
}

ZEST_CASE(shared_ptr_to_struct) {
    const auto result = json::schema_string<shared_struct>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("name":{"type":"string"},)"
                      R"("point":{"anyOf":[{)"
                      R"("$ref":"#/$defs/point2d"},)"
                      R"({"type":"null"}],"default":null}},)"
                      R"("required":["name"],)"
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

ZEST_CASE(optional_struct_field) {
    const auto result = json::schema_string<optional_struct>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("point":{"anyOf":[{)"
                      R"("$ref":"#/$defs/point2d"},)"
                      R"({"type":"null"}],"default":null},)"
                      R"("name":{"type":"string"}},)"
                      R"("required":["name"],)"
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

ZEST_CASE(defs_dedup_multi_ref) {
    const auto result = json::schema_string<multi_ref>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("a":{)"
                      R"("$ref":"#/$defs/point2d"},)"
                      R"("b":{)"
                      R"("$ref":"#/$defs/point2d"},)"
                      R"("list":{"type":"array",)"
                      R"("items":{)"
                      R"("$ref":"#/$defs/point2d"}}},)"
                      R"("required":["a","b","list"],)"
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

ZEST_CASE(optional_inner_field) {
    const auto result = json::schema_string<optional_inner>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("i":{"anyOf":[{)"
                      R"("$ref":"#/$defs/inner"},)"
                      R"({"type":"null"}],"default":null},)"
                      R"("name":{"type":"string"}},)"
                      R"("required":["name"],)"
                      R"("$defs":{)"
                      R"("inner":{"type":"object",)"
                      R"("properties":{)"
                      R"("a":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["a"]}}})");
}

ZEST_CASE(combo_mixed_fields) {
    const auto result = json::schema_string<combo>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("color":{)"
                      R"("type":"integer","minimum":-128,"maximum":127},)"
                      R"("label":{"anyOf":[{"type":"string"},)"
                      R"({"type":"null"}],"default":null},)"
                      R"("values":{"type":"array",)"
                      R"("items":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("attrs":{"type":"object",)"
                      R"("additionalProperties":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}}},)"
                      R"("required":["color","values","attrs"]})");
}

ZEST_CASE(combo_nested_struct_refs) {
    const auto result = json::schema_string<nested_combo>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("point":{)"
                      R"("$ref":"#/$defs/point2d"},)"
                      R"("color":{)"
                      R"("type":"integer","minimum":-128,"maximum":127},)"
                      R"("points":{"type":"array",)"
                      R"("items":{)"
                      R"("$ref":"#/$defs/point2d"}},)"
                      R"("named_points":{"type":"object",)"
                      R"("additionalProperties":{)"
                      R"("$ref":"#/$defs/point2d"}}},)"
                      R"("required":[)"
                      R"("point","color","points","named_points"],)"
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

ZEST_CASE(combo_vec_of_struct) {
    const auto result = json::schema_string<vec_of_struct>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("items":{"type":"array",)"
                      R"("items":{)"
                      R"("$ref":"#/$defs/point2d"}}},)"
                      R"("required":["items"],)"
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

ZEST_CASE(combo_deep_nesting) {
    const auto result = json::schema_string<deep_outer>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("dm":{)"
                      R"("$ref":"#/$defs/deep_middle"},)"
                      R"("n":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["dm","n"],)"
                      R"("$defs":{)"
                      R"("deep_inner":{"type":"object",)"
                      R"("properties":{)"
                      R"("c":{)"
                      R"("type":"integer","minimum":-128,"maximum":127},)"
                      R"("v":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["c","v"]},)"
                      R"("deep_middle":{"type":"object",)"
                      R"("properties":{)"
                      R"("di":{)"
                      R"("$ref":"#/$defs/deep_inner"},)"
                      R"("s":{"type":"string"}},)"
                      R"("required":["di","s"]}}})");
}

ZEST_CASE(combo_multi_map) {
    const auto result = json::schema_string<multi_map>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("a":{"type":"object",)"
                      R"("additionalProperties":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("b":{"type":"object",)"
                      R"("additionalProperties":{"type":"string"}}},)"
                      R"("required":["a","b"]})");
}

ZEST_CASE(combo_many_fields) {
    const auto result = json::schema_string<many_fields>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("a":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"("b":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"("c":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"("d":{"type":"string"},)"
                      R"("e":{"type":"boolean"},)"
                      R"("f":{"anyOf":[{"type":"number"},{"type":"null"}]}},)"
                      R"("required":["a","b","c","d","e","f"]})");
}

ZEST_CASE(combo_set_of_struct) {
    const auto result = json::schema_string<set_of_struct>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("ids":{"type":"array",)"
                      R"("items":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("name":{"type":"string"}},)"
                      R"("required":["ids","name"]})");
}

ZEST_CASE(combo_trivial_nested) {
    const auto result = json::schema_string<trivial_nested>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("p":{)"
                      R"("$ref":"#/$defs/point2d"},)"
                      R"("z":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["p","z"],)"
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

ZEST_CASE(self_referential_struct) {
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
        {"value", {}, 0, 0, type_info_of<std::int32_t>, false, false, false},
        {"next",
         {},
         0, 1,
         []() -> const type_info& { return opt_self; },
         false, false,
         false, true},
    };
    self_info.fields = {self_fields, 2};

    const auto result = json::schema_string(self_info).value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("value":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                      R"("next":{"anyOf":[{"$ref":"#"},{"type":"null"}]}},)"
                      R"("required":["value"]})");
}

ZEST_CASE(field_ordering_stability) {
    const static field_info ordered_fields[] = {
        {"zebra",  {}, 0, 0, type_info_of<std::string>,  false, false, false},
        {"alpha",  {}, 0, 1, type_info_of<std::int32_t>, false, false, false},
        {"middle", {}, 0, 2, type_info_of<bool>,         false, false, false},
        {"beta",   {}, 0, 3, type_info_of<double>,       false, false, false},
    };
    const static struct_type_info ordered_info = {
        {type_kind::structure, "ordered_struct"},
        false,
        false,
        {ordered_fields,       4               },
    };
    const auto result = json::schema_string(ordered_info).value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("zebra":{"type":"string"},)"
                      R"("alpha":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                      R"("middle":{"type":"boolean"},)"
                      R"("beta":{"anyOf":[{"type":"number"},{"type":"null"}]}},)"
                      R"("required":["zebra","alpha","middle","beta"]})");
}

ZEST_CASE(mutual_recursion) {
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
        {"value", {}, 0, 0, type_info_of<std::int32_t>, false, false, false},
        {"b", {}, 0, 1, []() -> const type_info& { return opt_b; }, false, false, false, true},
    };
    const static field_info fields_b[] = {
        {"name", {}, 0, 0, type_info_of<std::string>, false, false, false},
        {"a", {}, 0, 1, []() -> const type_info& { return opt_a; }, false, false, false, true},
    };
    info_a.fields = {fields_a, 2};
    info_b.fields = {fields_b, 2};

    const auto result = json::schema_string(info_a).value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("value":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                      R"("b":{"anyOf":[{"$ref":"#/$defs/node_b"},{"type":"null"}]}},)"
                      R"("required":["value"],)"
                      R"("$defs":{)"
                      R"("node_b":{"type":"object",)"
                      R"("properties":{)"
                      R"("name":{"type":"string"},)"
                      R"("a":{"anyOf":[{"$ref":"#"},{"type":"null"}]}},)"
                      R"("required":["name"]}}})");
}

};  // ZEST_SUITE(codec_json_schema_structs)

}  // namespace

}  // namespace kota::meta
