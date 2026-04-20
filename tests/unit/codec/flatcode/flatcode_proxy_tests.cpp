#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/codec/flatcode/flatcode.h"

namespace kota::codec {

using namespace meta;

namespace {

using flatcode::array_view;
using flatcode::map_view;
using flatcode::table_view;
using flatcode::to_flatcode;
using flatcode::tuple_view;
using flatcode::variant_view;

enum class color : std::int32_t { red = 0, green = 1, blue = 2 };

struct point {
    std::int32_t x;
    std::int32_t y;

    auto operator==(const point&) const -> bool = default;
};

struct address {
    std::string city;
    std::int32_t zip;

    auto operator==(const address&) const -> bool = default;
};

struct person {
    std::int32_t id;
    std::string name;
    point pos;
    std::vector<std::int32_t> scores;
    address addr;

    auto operator==(const person&) const -> bool = default;
};

TEST_SUITE(serde_flatcode_proxy) {

TEST_CASE(scalar_and_string_members_read_through_proxy) {
    const person input{
        .id = 7,
        .name = "alice",
        .pos = {.x = 10, .y = 20},
        .scores = {1, 2, 3},
        .addr = {.city = "sh", .zip = 200000},
    };

    auto encoded = to_flatcode(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<person>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    EXPECT_EQ(root[&person::id], 7);
    EXPECT_EQ(root[&person::name], "alice");
}

TEST_CASE(inline_struct_member_returns_by_value) {
    const person input{
        .id = 1,
        .name = "bob",
        .pos = {.x = 10, .y = 20},
        .scores = {},
        .addr = {.city = "x", .zip = 0},
    };

    auto encoded = to_flatcode(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<person>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    const auto pos = root[&person::pos];
    EXPECT_EQ(pos.x, 10);
    EXPECT_EQ(pos.y, 20);
}

TEST_CASE(scalar_vector_read_through_array_view) {
    const person input{
        .id = 1,
        .name = "n",
        .pos = {.x = 0, .y = 0},
        .scores = {1, 2, 3, 4},
        .addr = {.city = "x", .zip = 0},
    };

    auto encoded = to_flatcode(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<person>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto scores = root[&person::scores];
    ASSERT_TRUE(scores.valid());
    ASSERT_EQ(scores.size(), 4U);
    EXPECT_EQ(scores[0], 1);
    EXPECT_EQ(scores[1], 2);
    EXPECT_EQ(scores[2], 3);
    EXPECT_EQ(scores[3], 4);
}

TEST_CASE(nested_table_member_read_through_proxy) {
    const person input{
        .id = 1,
        .name = "n",
        .pos = {.x = 0, .y = 0},
        .scores = {},
        .addr = {.city = "tokyo", .zip = 100},
    };

    auto encoded = to_flatcode(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<person>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto addr = root[&person::addr];
    ASSERT_TRUE(addr.valid());
    EXPECT_EQ(addr[&address::city], "tokyo");
    EXPECT_EQ(addr[&address::zip], 100);
}

TEST_CASE(from_bytes_rejects_bad_magic) {
    const person input{.id = 1, .name = "x", .pos = {}, .scores = {}, .addr = {}};
    auto encoded = to_flatcode(input);
    ASSERT_TRUE(encoded.has_value());
    (*encoded)[0] ^= 0xFF;

    auto root = table_view<person>::from_bytes(*encoded);
    EXPECT_FALSE(root.valid());
}

TEST_CASE(pair_field_read_through_tuple_view) {
    struct pair_owner {
        std::pair<std::int32_t, std::string> kv;
        auto operator==(const pair_owner&) const -> bool = default;
    };

    const pair_owner input{
        .kv = {42, "hi"}
    };
    auto encoded = to_flatcode(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<pair_owner>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto kv = root[&pair_owner::kv];
    ASSERT_TRUE(kv.valid());
    EXPECT_EQ(kv.template get<0>(), 42);
    EXPECT_EQ(kv.template get<1>(), "hi");
}

TEST_CASE(variant_field_index_and_payload) {
    struct v_owner {
        std::variant<std::int32_t, std::string> choice;
        auto operator==(const v_owner&) const -> bool = default;
    };

    const v_owner input{.choice = std::string("pick")};
    auto encoded = to_flatcode(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<v_owner>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto choice = root[&v_owner::choice];
    ASSERT_TRUE(choice.valid());
    EXPECT_EQ(choice.index(), 1U);
    EXPECT_EQ(choice.template get<1>(), "pick");
}

TEST_CASE(map_field_contains_and_lookup) {
    struct m_owner {
        std::map<std::int32_t, std::string> table;
        auto operator==(const m_owner&) const -> bool = default;
    };

    const m_owner input{
        .table = {{1, "one"}, {2, "two"}, {3, "three"}}
    };
    auto encoded = to_flatcode(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<m_owner>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto table = root[&m_owner::table];
    ASSERT_TRUE(table.valid());
    EXPECT_EQ(table.size(), 3U);
    EXPECT_TRUE(table.contains(2));
    EXPECT_EQ(table[2], "two");
    EXPECT_EQ(table[1], "one");
    EXPECT_EQ(table[3], "three");
}

};  // TEST_SUITE(serde_flatcode_proxy)

}  // namespace

}  // namespace kota::codec
