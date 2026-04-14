#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "eventide/zest/zest.h"
#include "eventide/serde/schema/codegen/json_schema.h"
#include "eventide/serde/schema/virtual_schema.h"

namespace eventide::serde {

namespace {

struct point2d {
    std::int32_t x;
    std::int32_t y;
};

enum class color : std::int8_t { red = 0, green = 1, blue = 2 };

struct payload {
    point2d point;
    std::string name;
    std::vector<std::int32_t> values;
    std::map<std::string, std::int32_t> attrs;
};

struct with_optional {
    std::string name;
    std::optional<std::int32_t> age;
};

struct colored_payload {
    color col;
    std::string label;
    std::vector<point2d> points;
};

TEST_SUITE(serde_json_schema) {

TEST_CASE(simple_struct_has_properties_and_required) {
    const auto s = schema::codegen::json_schema::render(schema::type_info_of<point2d>());
    EXPECT_NE(s.find("\"$schema\""), std::string::npos);
    EXPECT_NE(s.find("\"type\":\"object\""), std::string::npos);
    EXPECT_NE(s.find("\"x\":{\"type\":\"integer\""), std::string::npos);
    EXPECT_NE(s.find("\"y\":{\"type\":\"integer\""), std::string::npos);
    EXPECT_NE(s.find("\"required\""), std::string::npos);
}

TEST_CASE(nested_struct_uses_ref_and_defs) {
    const auto s = schema::codegen::json_schema::render(schema::type_info_of<payload>());
    EXPECT_NE(s.find("\"$defs\""), std::string::npos);
    EXPECT_NE(s.find("\"$ref\""), std::string::npos);
}

TEST_CASE(enum_field_emits_enum_array) {
    const auto s = schema::codegen::json_schema::render(schema::type_info_of<colored_payload>());
    EXPECT_NE(s.find("\"enum\""), std::string::npos);
    EXPECT_NE(s.find("\"red\""), std::string::npos);
    EXPECT_NE(s.find("\"green\""), std::string::npos);
    EXPECT_NE(s.find("\"blue\""), std::string::npos);
}

TEST_CASE(optional_field_not_in_required) {
    const auto s = schema::codegen::json_schema::render(schema::type_info_of<with_optional>());
    // "name" should be required, "age" should not
    EXPECT_NE(s.find("\"name\""), std::string::npos);
    EXPECT_NE(s.find("\"age\""), std::string::npos);
    // required array should contain "name" but not "age"
    EXPECT_NE(s.find("\"required\":[\"name\"]"), std::string::npos);
}

TEST_CASE(vector_field_emits_array_with_items) {
    const auto s = schema::codegen::json_schema::render(schema::type_info_of<payload>());
    EXPECT_NE(s.find("\"type\":\"array\""), std::string::npos);
    EXPECT_NE(s.find("\"items\""), std::string::npos);
}

TEST_CASE(map_field_emits_additional_properties) {
    const auto s = schema::codegen::json_schema::render(schema::type_info_of<payload>());
    EXPECT_NE(s.find("\"additionalProperties\""), std::string::npos);
}

};  // TEST_SUITE(serde_json_schema)

}  // namespace

}  // namespace eventide::serde
