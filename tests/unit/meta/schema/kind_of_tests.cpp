#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "fixtures/schema/enums.h"
#include "fixtures/schema/primitives.h"
#include "kota/zest/zest.h"
#include "kota/meta/type_kind.h"

namespace kota::meta {

namespace {

namespace fx = ::kota::meta::fixtures;

TEST_SUITE(virtual_schema_kind_of) {

TEST_CASE(scalars) {
    STATIC_EXPECT_EQ(kind_of<bool>(), type_kind::boolean);
    STATIC_EXPECT_EQ(kind_of<std::int8_t>(), type_kind::int8);
    STATIC_EXPECT_EQ(kind_of<std::int16_t>(), type_kind::int16);
    STATIC_EXPECT_EQ(kind_of<int>(), type_kind::int32);
    STATIC_EXPECT_EQ(kind_of<std::int64_t>(), type_kind::int64);
    STATIC_EXPECT_EQ(kind_of<std::uint8_t>(), type_kind::uint8);
    STATIC_EXPECT_EQ(kind_of<std::uint16_t>(), type_kind::uint16);
    STATIC_EXPECT_EQ(kind_of<std::uint32_t>(), type_kind::uint32);
    STATIC_EXPECT_EQ(kind_of<std::uint64_t>(), type_kind::uint64);
    STATIC_EXPECT_EQ(kind_of<float>(), type_kind::float32);
    STATIC_EXPECT_EQ(kind_of<double>(), type_kind::float64);
    STATIC_EXPECT_EQ(kind_of<char>(), type_kind::character);
    STATIC_EXPECT_EQ(kind_of<std::string>(), type_kind::string);
    STATIC_EXPECT_EQ(kind_of<std::string_view>(), type_kind::string);
    STATIC_EXPECT_EQ(kind_of<std::nullptr_t>(), type_kind::null);
}

TEST_CASE(enums) {
    STATIC_EXPECT_EQ(kind_of<fx::Color>(), type_kind::enumeration);
    STATIC_EXPECT_EQ(kind_of<fx::SmallEnum>(), type_kind::enumeration);
}

TEST_CASE(compounds) {
    STATIC_EXPECT_EQ(kind_of<std::vector<int>>(), type_kind::array);
    STATIC_EXPECT_EQ(kind_of<std::set<int>>(), type_kind::set);
    STATIC_EXPECT_EQ(kind_of<std::map<std::string, int>>(), type_kind::map);
    STATIC_EXPECT_EQ(kind_of<std::optional<int>>(), type_kind::optional);
    STATIC_EXPECT_EQ(kind_of<std::unique_ptr<int>>(), type_kind::pointer);
    STATIC_EXPECT_EQ(kind_of<std::shared_ptr<int>>(), type_kind::pointer);
    STATIC_EXPECT_EQ(kind_of<std::variant<int, std::string>>(), type_kind::variant);
    STATIC_EXPECT_EQ(kind_of<std::tuple<int, float>>(), type_kind::tuple);
    STATIC_EXPECT_EQ(kind_of<std::pair<int, std::string>>(), type_kind::tuple);
    STATIC_EXPECT_EQ(kind_of<fx::SimpleStruct>(), type_kind::structure);
}

};  // TEST_SUITE(virtual_schema_kind_of)

}  // namespace

}  // namespace kota::meta
