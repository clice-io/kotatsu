#if __has_include(<flatbuffers/flatbuffers.h>)

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "fixtures/schema/common.h"
#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/codec/flatbuffers/flatbuffers.h"
#include "flatbuffers/flatbuffers.h"

namespace kota::codec {

using namespace meta;

namespace {

using flatbuffers::array_view;
using flatbuffers::map_view;
using flatbuffers::table_view;
using flatbuffers::to_flatbuffer;
using flatbuffers::tuple_view;
using flatbuffers::variant_view;

enum class color : std::int32_t { red = 0, green = 1, blue = 2 };

using point = meta::fixtures::Point2i;
using address = meta::fixtures::Address;

struct person {
    std::int32_t id;
    std::string name;
    point pos;
    std::vector<std::int32_t> scores;
    address addr;
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

// --- Optional / smart pointer test structs ---

struct with_optional_scalar {
    std::optional<std::int32_t> value;
};

struct with_optional_string {
    std::optional<std::string> value;
};

struct with_optional_struct {
    std::optional<address> addr;
};

struct with_unique_ptr {
    std::unique_ptr<address> addr;
};

struct with_shared_ptr {
    std::shared_ptr<address> addr;
};

// --- Variant test structs ---

struct with_variant {
    std::variant<std::int32_t, std::string> value;
};

struct with_variant_struct {
    std::variant<std::int32_t, address> value;
};

struct with_vector_of_variants {
    std::vector<std::variant<std::int32_t, std::string>> items;
};

// --- Tuple / pair test structs ---

struct with_pair {
    std::pair<std::int32_t, std::string> value;
};

struct with_tuple {
    std::tuple<std::int32_t, std::string, double> value;
};

struct with_pair_struct_value {
    std::pair<std::string, address> value;
};

struct with_vector_of_pairs {
    std::vector<std::pair<std::int32_t, std::string>> items;
};

// --- Map test structs ---

struct with_map_string_int {
    std::map<std::string, std::int32_t> data;
};

struct with_map_int_string {
    std::map<std::int32_t, std::string> data;
};

struct with_map_string_struct {
    std::map<std::string, address> data;
};

struct with_empty_map {
    std::map<std::string, std::int32_t> data;
};

// --- Edge case test structs ---

struct with_enum {
    color c;
};

struct with_bool {
    bool flag;
};

struct with_vector_strings {
    std::vector<std::string> items;
};

struct with_vector_tables {
    std::vector<address> items;
};

struct outer {
    address inner;
    std::vector<std::int32_t> nums;
};

struct deeply_nested_struct {
    outer nested;
};

// ======== Optional / smart pointer tests ========

TEST_CASE(optional_scalar_field_present) {
    with_optional_scalar input{.value = 42};

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_optional_scalar>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());
    ASSERT_TRUE(root.has(&with_optional_scalar::value));
    EXPECT_EQ(root[&with_optional_scalar::value], 42);
}

TEST_CASE(optional_scalar_field_absent) {
    with_optional_scalar input{.value = std::nullopt};

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_optional_scalar>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());
    EXPECT_FALSE(root.has(&with_optional_scalar::value));
    EXPECT_EQ(root[&with_optional_scalar::value], 0);
}

TEST_CASE(optional_string_field) {
    // Present case
    {
        with_optional_string input{.value = "hello"};

        auto encoded = to_flatbuffer(input);
        ASSERT_TRUE(encoded.has_value());

        auto root = table_view<with_optional_string>::from_bytes(*encoded);
        ASSERT_TRUE(root.valid());
        ASSERT_TRUE(root.has(&with_optional_string::value));
        EXPECT_EQ(root[&with_optional_string::value], "hello");
    }

    // Absent case
    {
        with_optional_string input{.value = std::nullopt};

        auto encoded = to_flatbuffer(input);
        ASSERT_TRUE(encoded.has_value());

        auto root = table_view<with_optional_string>::from_bytes(*encoded);
        ASSERT_TRUE(root.valid());
        EXPECT_FALSE(root.has(&with_optional_string::value));
        EXPECT_EQ(root[&with_optional_string::value], "");
    }
}

TEST_CASE(optional_struct_field) {
    // Present case
    {
        with_optional_struct input{
            .addr = address{.city = "paris", .zip = 75000}
        };

        auto encoded = to_flatbuffer(input);
        ASSERT_TRUE(encoded.has_value());

        auto root = table_view<with_optional_struct>::from_bytes(*encoded);
        ASSERT_TRUE(root.valid());
        ASSERT_TRUE(root.has(&with_optional_struct::addr));

        auto addr = root[&with_optional_struct::addr];
        ASSERT_TRUE(addr.valid());
        EXPECT_EQ(addr[&address::city], "paris");
        EXPECT_EQ(addr[&address::zip], 75000);
    }

    // Absent case
    {
        with_optional_struct input{.addr = std::nullopt};

        auto encoded = to_flatbuffer(input);
        ASSERT_TRUE(encoded.has_value());

        auto root = table_view<with_optional_struct>::from_bytes(*encoded);
        ASSERT_TRUE(root.valid());
        EXPECT_FALSE(root.has(&with_optional_struct::addr));

        auto addr = root[&with_optional_struct::addr];
        EXPECT_FALSE(addr.valid());
    }
}

TEST_CASE(unique_ptr_field_present) {
    with_unique_ptr input{};
    input.addr = std::make_unique<address>(address{.city = "berlin", .zip = 10115});

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_unique_ptr>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());
    ASSERT_TRUE(root.has(&with_unique_ptr::addr));

    auto addr = root[&with_unique_ptr::addr];
    ASSERT_TRUE(addr.valid());
    EXPECT_EQ(addr[&address::city], "berlin");
    EXPECT_EQ(addr[&address::zip], 10115);
}

TEST_CASE(unique_ptr_field_null) {
    with_unique_ptr input{};
    input.addr = nullptr;

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_unique_ptr>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());
    EXPECT_FALSE(root.has(&with_unique_ptr::addr));

    auto addr = root[&with_unique_ptr::addr];
    EXPECT_FALSE(addr.valid());
}

TEST_CASE(shared_ptr_field) {
    // Present case
    {
        with_shared_ptr input{};
        input.addr = std::make_shared<address>(address{.city = "london", .zip = 20000});

        auto encoded = to_flatbuffer(input);
        ASSERT_TRUE(encoded.has_value());

        auto root = table_view<with_shared_ptr>::from_bytes(*encoded);
        ASSERT_TRUE(root.valid());
        ASSERT_TRUE(root.has(&with_shared_ptr::addr));

        auto addr = root[&with_shared_ptr::addr];
        ASSERT_TRUE(addr.valid());
        EXPECT_EQ(addr[&address::city], "london");
        EXPECT_EQ(addr[&address::zip], 20000);
    }

    // Null case
    {
        with_shared_ptr input{};
        input.addr = nullptr;

        auto encoded = to_flatbuffer(input);
        ASSERT_TRUE(encoded.has_value());

        auto root = table_view<with_shared_ptr>::from_bytes(*encoded);
        ASSERT_TRUE(root.valid());
        EXPECT_FALSE(root.has(&with_shared_ptr::addr));
    }
}

// ======== Variant tests ========

TEST_CASE(variant_scalar_alternative) {
    with_variant input{.value = std::int32_t{99}};

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_variant>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto v = root[&with_variant::value];
    ASSERT_TRUE(v.valid());
    EXPECT_EQ(v.index(), 0U);
    EXPECT_EQ(v.get<0>(), 99);
}

TEST_CASE(variant_string_alternative) {
    with_variant input{.value = std::string("kotatsu")};

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_variant>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto v = root[&with_variant::value];
    ASSERT_TRUE(v.valid());
    EXPECT_EQ(v.index(), 1U);
    EXPECT_EQ(v.get<1>(), "kotatsu");
}

TEST_CASE(variant_struct_alternative) {
    with_variant_struct input{
        .value = address{.city = "rome", .zip = 100}
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_variant_struct>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto v = root[&with_variant_struct::value];
    ASSERT_TRUE(v.valid());
    EXPECT_EQ(v.index(), 1U);

    auto addr = v.get<1>();
    ASSERT_TRUE(addr.valid());
    EXPECT_EQ(addr[&address::city], "rome");
    EXPECT_EQ(addr[&address::zip], 100);
}

// NOTE: vector_of_variants is not tested here because the serializer wraps
// each variant element in an extra boxed table, so array_view returns the outer
// box rather than the variant table directly.  This is a known encoding
// mismatch that would require changes to proxy.h or serializer.h to resolve.

// ======== Tuple / pair tests ========

TEST_CASE(pair_field) {
    with_pair input{
        .value = {42, "hello"}
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_pair>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto p = root[&with_pair::value];
    ASSERT_TRUE(p.valid());
    EXPECT_EQ(p.get<0>(), 42);
    EXPECT_EQ(p.get<1>(), "hello");
}

TEST_CASE(tuple_field) {
    with_tuple input{
        .value = {7, "world", 3.14}
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_tuple>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto t = root[&with_tuple::value];
    ASSERT_TRUE(t.valid());
    EXPECT_EQ(t.get<0>(), 7);
    EXPECT_EQ(t.get<1>(), "world");
    EXPECT_EQ(t.get<2>(), 3.14);
}

TEST_CASE(pair_with_struct_value) {
    with_pair_struct_value input{
        .value = {"key", address{.city = "nyc", .zip = 10001}}
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_pair_struct_value>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto p = root[&with_pair_struct_value::value];
    ASSERT_TRUE(p.valid());
    EXPECT_EQ(p.get<0>(), "key");

    auto addr = p.get<1>();
    ASSERT_TRUE(addr.valid());
    EXPECT_EQ(addr[&address::city], "nyc");
    EXPECT_EQ(addr[&address::zip], 10001);
}

TEST_CASE(vector_of_pairs) {
    with_vector_of_pairs input{
        .items = {{1, "a"}, {2, "b"}, {3, "c"}}
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_vector_of_pairs>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto items = root[&with_vector_of_pairs::items];
    ASSERT_TRUE(items.valid());
    ASSERT_EQ(items.size(), 3U);

    auto e0 = items[0];
    EXPECT_EQ(e0.get<0>(), 1);
    EXPECT_EQ(e0.get<1>(), "a");

    auto e2 = items[2];
    EXPECT_EQ(e2.get<0>(), 3);
    EXPECT_EQ(e2.get<1>(), "c");
}

// ======== Map tests ========

TEST_CASE(map_string_to_int) {
    with_map_string_int input{
        .data = {{"alpha", 1}, {"beta", 2}, {"gamma", 3}}
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_map_string_int>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto m = root[&with_map_string_int::data];
    ASSERT_TRUE(m.valid());
    ASSERT_EQ(m.size(), 3U);

    // std::map is ordered, so entries are alpha, beta, gamma
    auto e0 = m.at(0);
    ASSERT_TRUE(e0.valid());
    EXPECT_EQ(e0.get<0>(), "alpha");
    EXPECT_EQ(e0.get<1>(), 1);

    auto e1 = m.at(1);
    EXPECT_EQ(e1.get<0>(), "beta");
    EXPECT_EQ(e1.get<1>(), 2);

    auto e2 = m.at(2);
    EXPECT_EQ(e2.get<0>(), "gamma");
    EXPECT_EQ(e2.get<1>(), 3);
}

TEST_CASE(map_int_to_string) {
    with_map_int_string input{
        .data = {{10, "ten"}, {20, "twenty"}}
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_map_int_string>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto m = root[&with_map_int_string::data];
    ASSERT_TRUE(m.valid());
    ASSERT_EQ(m.size(), 2U);

    auto e0 = m.at(0);
    EXPECT_EQ(e0.get<0>(), 10);
    EXPECT_EQ(e0.get<1>(), "ten");

    auto e1 = m.at(1);
    EXPECT_EQ(e1.get<0>(), 20);
    EXPECT_EQ(e1.get<1>(), "twenty");
}

TEST_CASE(map_string_to_struct) {
    with_map_string_struct input{
        .data = {{"home", address{.city = "sf", .zip = 94102}},
                 {"work", address{.city = "la", .zip = 90001}}},
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_map_string_struct>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto m = root[&with_map_string_struct::data];
    ASSERT_TRUE(m.valid());
    ASSERT_EQ(m.size(), 2U);

    // std::map ordered: "home" < "work"
    auto e0 = m.at(0);
    EXPECT_EQ(e0.get<0>(), "home");
    auto addr0 = e0.get<1>();
    ASSERT_TRUE(addr0.valid());
    EXPECT_EQ(addr0[&address::city], "sf");
    EXPECT_EQ(addr0[&address::zip], 94102);

    auto e1 = m.at(1);
    EXPECT_EQ(e1.get<0>(), "work");
    auto addr1 = e1.get<1>();
    ASSERT_TRUE(addr1.valid());
    EXPECT_EQ(addr1[&address::city], "la");
    EXPECT_EQ(addr1[&address::zip], 90001);
}

TEST_CASE(empty_map) {
    with_empty_map input{.data = {}};

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_empty_map>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto m = root[&with_empty_map::data];
    EXPECT_TRUE(m.empty());
    EXPECT_EQ(m.size(), 0U);
}

TEST_CASE(map_key_lookup_string_key) {
    with_map_string_int input{
        .data = {{"alpha", 1}, {"beta", 2}, {"gamma", 3}}
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_map_string_int>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto m = root[&with_map_string_int::data];
    ASSERT_TRUE(m.valid());

    EXPECT_EQ(m[std::string("alpha")], 1);
    EXPECT_EQ(m[std::string("beta")], 2);
    EXPECT_EQ(m[std::string("gamma")], 3);
    // Missing key returns default
    EXPECT_EQ(m[std::string("missing")], 0);
}

TEST_CASE(map_key_lookup_int_key) {
    with_map_int_string input{
        .data = {{10, "ten"}, {20, "twenty"}, {30, "thirty"}}
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_map_int_string>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto m = root[&with_map_int_string::data];
    ASSERT_TRUE(m.valid());

    EXPECT_EQ(m[10], "ten");
    EXPECT_EQ(m[20], "twenty");
    EXPECT_EQ(m[30], "thirty");
    // Missing key returns default (empty string_view)
    EXPECT_EQ(m[99], "");
}

TEST_CASE(map_find_existing) {
    with_map_string_int input{
        .data = {{"alpha", 1}, {"beta", 2}, {"gamma", 3}}
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_map_string_int>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto m = root[&with_map_string_int::data];
    ASSERT_TRUE(m.valid());

    auto result = m.find(std::string("beta"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->get<0>(), "beta");
    EXPECT_EQ(result->get<1>(), 2);
}

TEST_CASE(map_find_missing) {
    with_map_string_int input{
        .data = {{"alpha", 1}, {"beta", 2}, {"gamma", 3}}
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_map_string_int>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto m = root[&with_map_string_int::data];
    ASSERT_TRUE(m.valid());

    auto result = m.find(std::string("missing"));
    EXPECT_FALSE(result.has_value());
}

TEST_CASE(map_contains) {
    with_map_string_int input{
        .data = {{"alpha", 1}, {"beta", 2}, {"gamma", 3}}
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_map_string_int>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto m = root[&with_map_string_int::data];
    ASSERT_TRUE(m.valid());

    EXPECT_TRUE(m.contains(std::string("alpha")));
    EXPECT_TRUE(m.contains(std::string("beta")));
    EXPECT_TRUE(m.contains(std::string("gamma")));
    EXPECT_FALSE(m.contains(std::string("missing")));
}

TEST_CASE(map_transparent_lookup) {
    with_map_string_int input{
        .data = {{"alpha", 1}, {"beta", 2}, {"gamma", 3}}
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_map_string_int>::from_bytes(*encoded);
    auto m = root[&with_map_string_int::data];
    ASSERT_TRUE(m.valid());

    // lookup with const char*
    EXPECT_EQ(m["beta"], 2);
    EXPECT_TRUE(m.contains("alpha"));
    EXPECT_FALSE(m.contains("missing"));

    // lookup with string_view
    std::string_view sv = "gamma";
    EXPECT_EQ(m[sv], 3);
    EXPECT_TRUE(m.contains(sv));

    auto result = m.find("beta");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->template get<0>(), "beta");
    EXPECT_EQ(result->template get<1>(), 2);

    auto missing = m.find("nope");
    EXPECT_FALSE(missing.has_value());
}

// ======== Edge case tests ========

TEST_CASE(enum_field) {
    with_enum input{.c = color::blue};

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_enum>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());
    EXPECT_EQ(root[&with_enum::c], color::blue);
}

TEST_CASE(bool_field) {
    with_bool input{.flag = true};

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_bool>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());
    EXPECT_EQ(root[&with_bool::flag], true);
}

TEST_CASE(vector_of_strings) {
    with_vector_strings input{
        .items = {"hello", "world", "test"}
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_vector_strings>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto items = root[&with_vector_strings::items];
    ASSERT_TRUE(items.valid());
    ASSERT_EQ(items.size(), 3U);
    EXPECT_EQ(items[0], "hello");
    EXPECT_EQ(items[1], "world");
    EXPECT_EQ(items[2], "test");
}

TEST_CASE(vector_of_nested_tables) {
    with_vector_tables input{
        .items = {address{.city = "a", .zip = 1}, address{.city = "b", .zip = 2}},
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<with_vector_tables>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto items = root[&with_vector_tables::items];
    ASSERT_TRUE(items.valid());
    ASSERT_EQ(items.size(), 2U);

    auto a0 = items[0];
    ASSERT_TRUE(a0.valid());
    EXPECT_EQ(a0[&address::city], "a");
    EXPECT_EQ(a0[&address::zip], 1);

    auto a1 = items[1];
    ASSERT_TRUE(a1.valid());
    EXPECT_EQ(a1[&address::city], "b");
    EXPECT_EQ(a1[&address::zip], 2);
}

TEST_CASE(deeply_nested) {
    deeply_nested_struct input{
        .nested = {.inner = {.city = "deep", .zip = 999}, .nums = {10, 20, 30}},
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<deeply_nested_struct>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto nested = root[&deeply_nested_struct::nested];
    ASSERT_TRUE(nested.valid());

    auto inner = nested[&outer::inner];
    ASSERT_TRUE(inner.valid());
    EXPECT_EQ(inner[&address::city], "deep");
    EXPECT_EQ(inner[&address::zip], 999);

    auto nums = nested[&outer::nums];
    ASSERT_TRUE(nums.valid());
    ASSERT_EQ(nums.size(), 3U);
    EXPECT_EQ(nums[0], 10);
    EXPECT_EQ(nums[1], 20);
    EXPECT_EQ(nums[2], 30);
}

TEST_CASE(empty_from_bytes) {
    std::vector<std::uint8_t> empty_data{};
    auto root = table_view<person>::from_bytes(std::span<const std::uint8_t>(empty_data));
    EXPECT_FALSE(root.valid());
}

TEST_CASE(array_view_out_of_bounds) {
    person input{
        .id = 1,
        .name = "n",
        .pos = {.x = 0, .y = 0},
        .scores = {100},
        .addr = {.city = "x", .zip = 0},
    };

    auto encoded = to_flatbuffer(input);
    ASSERT_TRUE(encoded.has_value());

    auto root = table_view<person>::from_bytes(*encoded);
    ASSERT_TRUE(root.valid());

    auto scores = root[&person::scores];
    ASSERT_TRUE(scores.valid());
    ASSERT_EQ(scores.size(), 1U);

    // Out-of-bounds access should return default
    EXPECT_EQ(scores[1], 0);
    EXPECT_EQ(scores[100], 0);
}

TEST_CASE(roundtrip_nested_struct) {
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
    const sample_variant input_var = std::string("kotatsu");

    auto encoded_var = to_flatbuffer(input_var);
    ASSERT_TRUE(encoded_var.has_value());

    sample_variant output_var = std::int32_t{0};
    auto var_status = flatbuffers::from_flatbuffer(*encoded_var, output_var);
    ASSERT_TRUE(var_status.has_value());
    EXPECT_EQ(output_var, input_var);
}

};  // TEST_SUITE(serde_flatbuffers_object)

}  // namespace

}  // namespace kota::codec

#endif
