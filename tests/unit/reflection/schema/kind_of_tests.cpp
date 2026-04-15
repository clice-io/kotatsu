#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "eventide/zest/zest.h"
#include "eventide/reflection/type_kind.h"

namespace eventide::refl {

namespace test_schema {

enum class color { red, green, blue };

enum class small_enum : std::int8_t { a = 1, b = 2 };

struct SimpleStruct {
    int x;
    std::string name;
};

}  // namespace test_schema

namespace {

TEST_SUITE(virtual_schema_kind_of) {

TEST_CASE(scalars) {
    EXPECT_EQ(kind_of<bool>(), type_kind::boolean);
    EXPECT_EQ(kind_of<std::int8_t>(), type_kind::int8);
    EXPECT_EQ(kind_of<std::int16_t>(), type_kind::int16);
    EXPECT_EQ(kind_of<int>(), type_kind::int32);
    EXPECT_EQ(kind_of<std::int64_t>(), type_kind::int64);
    EXPECT_EQ(kind_of<std::uint8_t>(), type_kind::uint8);
    EXPECT_EQ(kind_of<std::uint16_t>(), type_kind::uint16);
    EXPECT_EQ(kind_of<std::uint32_t>(), type_kind::uint32);
    EXPECT_EQ(kind_of<std::uint64_t>(), type_kind::uint64);
    EXPECT_EQ(kind_of<float>(), type_kind::float32);
    EXPECT_EQ(kind_of<double>(), type_kind::float64);
    EXPECT_EQ(kind_of<char>(), type_kind::character);
    EXPECT_EQ(kind_of<std::string>(), type_kind::string);
    EXPECT_EQ(kind_of<std::string_view>(), type_kind::string);
    EXPECT_EQ(kind_of<std::nullptr_t>(), type_kind::null);
}

TEST_CASE(enums) {
    EXPECT_EQ(kind_of<test_schema::color>(), type_kind::enumeration);
    EXPECT_EQ(kind_of<test_schema::small_enum>(), type_kind::enumeration);
}

TEST_CASE(compounds) {
    EXPECT_EQ(kind_of<std::vector<int>>(), type_kind::array);
    EXPECT_EQ(kind_of<std::set<int>>(), type_kind::set);
    EXPECT_EQ(kind_of<std::map<std::string, int>>(), type_kind::map);
    EXPECT_EQ(kind_of<std::optional<int>>(), type_kind::optional);
    EXPECT_EQ(kind_of<std::unique_ptr<int>>(), type_kind::pointer);
    EXPECT_EQ(kind_of<std::shared_ptr<int>>(), type_kind::pointer);
    EXPECT_EQ(kind_of<std::variant<int, std::string>>(), type_kind::variant);
    EXPECT_EQ(kind_of<std::tuple<int, float>>(), type_kind::tuple);
    EXPECT_EQ(kind_of<std::pair<int, std::string>>(), type_kind::tuple);
    EXPECT_EQ(kind_of<test_schema::SimpleStruct>(), type_kind::structure);
}

};  // TEST_SUITE(virtual_schema_kind_of)

}  // namespace

}  // namespace eventide::refl
