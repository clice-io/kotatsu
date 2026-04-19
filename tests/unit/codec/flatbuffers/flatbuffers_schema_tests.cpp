#if __has_include(<flatbuffers/flatbuffer_builder.h>)

#include <map>
#include <string>
#include <vector>

#include "fixtures/schema/common.h"
#include "kota/zest/zest.h"
#include "kota/codec/flatbuffers/schema.h"

namespace kota::codec {

namespace {

using point2d = meta::fixtures::Point2i;

struct payload {
    point2d point;
    std::string name;
    std::vector<std::int32_t> values;
    std::map<std::string, std::int32_t> attrs;
};

TEST_SUITE(serde_flatbuffers_schema) {

TEST_CASE(trivial_struct_maps_to_schema_struct) {
    ASSERT_TRUE((flatbuffers::is_schema_struct_v<point2d>));
    ASSERT_FALSE((flatbuffers::is_schema_struct_v<payload>));
}

TEST_CASE(map_field_emits_binary_search_entry_table) {
    const auto schema = flatbuffers::render<payload>();
    const auto point_name = flatbuffers::type_identifier<point2d>();
    const auto payload_name = flatbuffers::type_identifier<payload>();
    const auto map_entry_name = payload_name + "_attrsEntry";

    EXPECT_NE(schema.find("struct " + point_name), std::string::npos);
    EXPECT_NE(schema.find("table " + payload_name), std::string::npos);
    EXPECT_NE(schema.find("table " + map_entry_name), std::string::npos);
    EXPECT_NE(schema.find("key:string (key);"), std::string::npos);
    EXPECT_NE(schema.find("attrs:[" + map_entry_name + "];"), std::string::npos);
}

TEST_CASE(sorted_entries_support_binary_search_lookup) {
    const std::map<std::string, int> input{
        {"zeta",  3},
        {"alpha", 1},
        {"mid",   2},
    };

    auto entries = flatbuffers::to_sorted_entries(input);
    ASSERT_EQ(entries.size(), 3U);
    EXPECT_EQ(entries[0].key, "alpha");
    EXPECT_EQ(entries[1].key, "mid");
    EXPECT_EQ(entries[2].key, "zeta");

    auto found = flatbuffers::bsearch_entry(entries, std::string("mid"));
    ASSERT_TRUE(found != nullptr);
    EXPECT_EQ(found->value, 2);

    auto missing = flatbuffers::bsearch_entry(entries, std::string("none"));
    EXPECT_TRUE(missing == nullptr);
}

};  // TEST_SUITE(serde_flatbuffers_schema)

}  // namespace

}  // namespace kota::codec

#endif
