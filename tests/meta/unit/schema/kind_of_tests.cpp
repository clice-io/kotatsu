#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "fixtures/enums.h"
#include "fixtures/structs.h"
#include "kota/zest/zest.h"
#include "kota/meta/type_kind.h"

namespace kota::meta {

namespace {

namespace fx = ::kota::test;

ZEST_SUITE(meta_schema_kind_of) {

ZEST_CASE(scalars) {
    ZSTATIC_EXPECT(kind_of<bool>() == type_kind::boolean);
    ZSTATIC_EXPECT(kind_of<std::int8_t>() == type_kind::int8);
    ZSTATIC_EXPECT(kind_of<std::int16_t>() == type_kind::int16);
    ZSTATIC_EXPECT(kind_of<int>() == type_kind::int32);
    ZSTATIC_EXPECT(kind_of<std::int64_t>() == type_kind::int64);
    ZSTATIC_EXPECT(kind_of<std::uint8_t>() == type_kind::uint8);
    ZSTATIC_EXPECT(kind_of<std::uint16_t>() == type_kind::uint16);
    ZSTATIC_EXPECT(kind_of<std::uint32_t>() == type_kind::uint32);
    ZSTATIC_EXPECT(kind_of<std::uint64_t>() == type_kind::uint64);
    ZSTATIC_EXPECT(kind_of<float>() == type_kind::float32);
    ZSTATIC_EXPECT(kind_of<double>() == type_kind::float64);
    ZSTATIC_EXPECT(kind_of<char>() == type_kind::character);
    ZSTATIC_EXPECT(kind_of<std::string>() == type_kind::string);
    ZSTATIC_EXPECT(kind_of<std::string_view>() == type_kind::string);
    ZSTATIC_EXPECT(kind_of<std::nullptr_t>() == type_kind::null);
}

ZEST_CASE(enums) {
    ZSTATIC_EXPECT(kind_of<fx::Color>() == type_kind::enumeration);
    ZSTATIC_EXPECT(kind_of<fx::SmallEnum>() == type_kind::enumeration);
}

ZEST_CASE(compounds) {
    ZSTATIC_EXPECT(kind_of<std::vector<int>>() == type_kind::array);
    ZSTATIC_EXPECT(kind_of<std::set<int>>() == type_kind::set);
    ZSTATIC_EXPECT(kind_of<std::map<std::string, int>>() == type_kind::map);
    ZSTATIC_EXPECT(kind_of<std::optional<int>>() == type_kind::optional);
    ZSTATIC_EXPECT(kind_of<std::unique_ptr<int>>() == type_kind::pointer);
    ZSTATIC_EXPECT(kind_of<std::shared_ptr<int>>() == type_kind::pointer);
    ZSTATIC_EXPECT(kind_of<std::variant<int, std::string>>() == type_kind::variant);
    ZSTATIC_EXPECT(kind_of<std::tuple<int, float>>() == type_kind::tuple);
    ZSTATIC_EXPECT(kind_of<std::pair<int, std::string>>() == type_kind::tuple);
    ZSTATIC_EXPECT(kind_of<fx::SimpleStruct>() == type_kind::structure);
}

};  // ZEST_SUITE(meta_schema_kind_of)

}  // namespace

}  // namespace kota::meta
