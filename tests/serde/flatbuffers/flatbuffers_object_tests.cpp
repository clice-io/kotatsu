#if __has_include(<flatbuffers/flatbuffers.h>)

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "eventide/zest/zest.h"
#include "eventide/serde/flatbuffers/flatbuffers.h"
#include "eventide/serde/serde/attrs.h"
#include <flatbuffers/flatbuffers.h>

namespace eventide::serde {

namespace {

using flatbuffers::table_view;
using flatbuffers::to_flatbuffer;

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

struct with_skip {
    std::int32_t a;
    skip<std::int32_t> internal;
    std::int32_t c;
};

TEST_SUITE(serde_flatbuffers_object) {

TEST_CASE(trivial_struct_field_serializes_as_inline_struct) {
    const person input{
        .id = 7,
        .name = "alice",
        .pos = {.x = 10, .y = 20},
        .scores = {1, 2, 3},
        .addr = {.city = "sh", .zip = 200000},
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());
    ASSERT_TRUE(::flatbuffers::BufferHasIdentifier(encoded->data(), "EVTO"));

    auto root = table_view<person>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    EXPECT_EQ(root[&person::id], 7);
    EXPECT_EQ(root[&person::name], "alice");

    const auto pos = root[&person::pos];
    EXPECT_EQ(pos.x, 10);
    EXPECT_EQ(pos.y, 20);

    auto scores = root[&person::scores];
    ASSERT_TRUE(scores.valid());
    ASSERT_EQ(scores.size(), 3U);
    EXPECT_EQ(scores[0], 1);
    EXPECT_EQ(scores[1], 2);
    EXPECT_EQ(scores[2], 3);

    auto addr = root[&person::addr];
    ASSERT_TRUE(addr.valid());
    EXPECT_EQ(addr[&address::city], "sh");
    EXPECT_EQ(addr[&address::zip], 200000);
}

TEST_CASE(non_trivial_nested_object_serializes_as_table_offset) {
    const person input{
        .id = 1,
        .name = "n",
        .pos = {.x = 1, .y = 2},
        .scores = {},
        .addr = {.city = "tokyo", .zip = 100},
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<person>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto addr = root[&person::addr];
    ASSERT_TRUE(addr.valid());
    EXPECT_EQ(addr[&address::city], "tokyo");
    EXPECT_EQ(addr[&address::zip], 100);
}

TEST_CASE(skip_attr_keeps_field_index_layout) {
    with_skip input{};
    input.a = 3;
    input.internal = 999;
    input.c = 5;

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_skip>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    EXPECT_EQ(root[&with_skip::a], 3);
    EXPECT_FALSE(root.has(&with_skip::internal));
    EXPECT_EQ(root[&with_skip::c], 5);
}

TEST_CASE(vector_of_trivial_struct_serializes_as_struct_vector) {
    struct route {
        std::vector<point> points;
    };

    const route input{
        .points = {{.x = 1, .y = 2}, {.x = 3, .y = 4}},
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<route>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto points = root[&route::points];
    ASSERT_TRUE(points.valid());
    ASSERT_EQ(points.size(), 2U);

    const auto p0 = points[0];
    const auto p1 = points[1];
    EXPECT_EQ(p0.x, 1);
    EXPECT_EQ(p0.y, 2);
    EXPECT_EQ(p1.x, 3);
    EXPECT_EQ(p1.y, 4);
}

TEST_CASE(root_vector_preserves_scalar_vector_encoding) {
    const std::vector<std::int32_t> input{3, 5, 8};

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    const auto* root = ::flatbuffers::GetRoot<::flatbuffers::Table>(encoded->data());
    ASSERT_TRUE(root != nullptr);

    const auto* vec = root->GetPointer<const ::flatbuffers::Vector<std::int32_t>*>(4);
    ASSERT_TRUE(vec != nullptr);
    ASSERT_EQ(vec->size(), 3U);
    EXPECT_EQ(vec->Get(0), 3);
    EXPECT_EQ(vec->Get(1), 5);
    EXPECT_EQ(vec->Get(2), 8);
}

TEST_CASE(roundtrip_person_with_from_flatbuffer) {
    const person input{
        .id = 7,
        .name = "alice",
        .pos = {.x = 10, .y = 20},
        .scores = {1, 2, 3},
        .addr = {.city = "sh", .zip = 200000},
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    person output{};
    auto status = flatbuffers::from_flatbuffer(*encoded, output);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(output, input);
}

TEST_CASE(roundtrip_root_vector_and_variant) {
    const std::vector<std::int32_t> input_vec{3, 5, 8};
    auto encoded_vec = to_flatbuffer(input_vec);
    ASSERT_TRUE(encoded_vec.has_value());

    std::vector<std::int32_t> output_vec{};
    auto vec_status = flatbuffers::from_flatbuffer(*encoded_vec, output_vec);
    ASSERT_TRUE(vec_status.has_value());
    EXPECT_EQ(output_vec, input_vec);

    using sample_variant = std::variant<std::int32_t, std::string>;
    const sample_variant input_var = std::string("eventide");

    auto encoded_var = to_flatbuffer(input_var);
    ASSERT_TRUE(encoded_var.has_value());

    sample_variant output_var = std::int32_t{0};
    auto var_status = flatbuffers::from_flatbuffer(*encoded_var, output_var);
    ASSERT_TRUE(var_status.has_value());
    EXPECT_EQ(output_var, input_var);
}

};  // TEST_SUITE(serde_flatbuffers_object)

}  // namespace

}  // namespace eventide::serde

#endif
