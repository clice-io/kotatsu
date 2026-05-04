#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "fixtures/schema/common.h"
#include "fixtures/schema/tagged.h"
#include "kota/zest/zest.h"
#include "kota/codec/json/schema.h"

namespace kota::codec {

namespace {

namespace json = kota::codec::json;

using namespace meta::fixtures;

struct ComboStruct {
    std::string name;
    std::optional<std::int32_t> age;
    std::vector<std::string> tags;
    std::map<std::string, std::int32_t> scores;
};

struct NestedContainers {
    std::map<std::string, std::vector<Point2i>> groups;
};

TEST_SUITE(json_schema_snapshot) {

TEST_CASE(person) {
    ASSERT_SNAPSHOT(json::schema_string<Person>(true).value(), "person");
}

TEST_CASE(person_with_scores) {
    ASSERT_SNAPSHOT(json::schema_string<PersonWithScores>(true).value(), "person_with_scores");
}

TEST_CASE(combo_struct) {
    ASSERT_SNAPSHOT(json::schema_string<ComboStruct>(true).value(), "combo_struct");
}

TEST_CASE(nested_containers) {
    ASSERT_SNAPSHOT(json::schema_string<NestedContainers>(true).value(), "nested_containers");
}

TEST_CASE(external_tagged) {
    ASSERT_SNAPSHOT(json::schema_string<ExternalTagged>(true).value(), "external_tagged");
}

TEST_CASE(internal_tagged) {
    ASSERT_SNAPSHOT(json::schema_string<InternalTagged>(true).value(), "internal_tagged");
}

TEST_CASE(adjacent_tagged) {
    ASSERT_SNAPSHOT(json::schema_string<AdjacentTagged>(true).value(), "adjacent_tagged");
}

TEST_CASE(tagged_field_struct) {
    ASSERT_SNAPSHOT(json::schema_string<TaggedFieldStruct>(true).value(), "tagged_field_struct");
}

};  // TEST_SUITE(json_schema_snapshot)

}  // namespace

}  // namespace kota::codec
