#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "codec/harness/fixtures/structs.h"
#include "fixtures/tagged.h"
#include "kota/zest/zest.h"
#include "kota/codec/json/schema.h"

namespace kota::codec {

namespace {

namespace json = kota::codec::json;

struct ComboStruct {
    std::string name;
    std::optional<std::int32_t> age;
    std::vector<std::string> tags;
    std::map<std::string, std::int32_t> scores;
};

struct NestedContainers {
    std::map<std::string, std::vector<test::Point>> groups;
};

ZEST_SUITE(codec_json_schema_snapshot) {

ZEST_CASE(person) {
    ZEXPECT(zest::snapshot(json::schema_string<test::Person>(true).value(), "person"));
}

ZEST_CASE(person_with_scores) {
    ZEXPECT(zest::snapshot(json::schema_string<test::PersonWithScores>(true).value(),
                           "person_with_scores"));
}

ZEST_CASE(combo_struct) {
    ZEXPECT(zest::snapshot(json::schema_string<ComboStruct>(true).value(), "combo_struct"));
}

ZEST_CASE(nested_containers) {
    ZASSERT(
        zest::snapshot(json::schema_string<NestedContainers>(true).value(), "nested_containers"));
}

ZEST_CASE(external_tagged) {
    ZASSERT(
        zest::snapshot(json::schema_string<test::ExternalTagged>(true).value(), "external_tagged"));
}

ZEST_CASE(internal_tagged) {
    ZASSERT(
        zest::snapshot(json::schema_string<test::InternalTagged>(true).value(), "internal_tagged"));
}

ZEST_CASE(adjacent_tagged) {
    ZASSERT(
        zest::snapshot(json::schema_string<test::AdjacentTagged>(true).value(), "adjacent_tagged"));
}

ZEST_CASE(tagged_field_struct) {
    ZEXPECT(zest::snapshot(json::schema_string<test::TaggedFieldStruct>(true).value(),
                           "tagged_field_struct"));
}

};  // ZEST_SUITE(codec_json_schema_snapshot)

}  // namespace

}  // namespace kota::codec
