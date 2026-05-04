#if __has_include(<toml++/toml.hpp>)

#include <array>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "fixtures/schema/common.h"
#include "kota/zest/zest.h"
#include "kota/codec/toml/toml.h"

namespace kota::codec {

namespace {

using toml::from_toml;
using toml::parse;
using toml::to_string;
using toml::to_toml;

using person = meta::fixtures::PersonWithScores;

struct payload_with_extra {
    int id = 0;
    ::toml::table extra;
};

TEST_SUITE(serde_toml) {

TEST_CASE(struct_roundtrip_with_dom) {
    const person input{
        .id = 7,
        .name = "alice",
        .scores = {1, 2, 3},
        .active = true,
    };

    auto dom = to_toml(input);
    ASSERT_TRUE(dom.has_value());
    ASSERT_TRUE(dom->contains("id"));
    ASSERT_TRUE(dom->contains("name"));
    ASSERT_TRUE(dom->contains("scores"));
    ASSERT_TRUE(dom->contains("active"));

    person output{};
    auto status = from_toml(*dom, output);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(output, input);
}

TEST_CASE(parse_and_to_string_roundtrip) {
    constexpr std::string_view input = R"(
id = 9
name = "bob"
scores = [4, 5]
active = true
)";

    auto parsed = parse<person>(input);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->id, 9);
    EXPECT_EQ(parsed->name, "bob");
    EXPECT_EQ(parsed->scores, std::vector<int>({4, 5}));
    EXPECT_EQ(parsed->active, true);

    auto encoded = to_string(*parsed);
    ASSERT_TRUE(encoded.has_value());

    auto reparsed = parse<person>(*encoded);
    ASSERT_TRUE(reparsed.has_value());
    EXPECT_EQ(*reparsed, *parsed);
}

TEST_CASE(dynamic_dom_field_roundtrip) {
    payload_with_extra input{};
    input.id = 1;
    input.extra.insert_or_assign("city", "shanghai");
    input.extra.insert_or_assign("zip", 200000);

    ::toml::array tags;
    tags.push_back("a");
    tags.push_back("b");
    input.extra.insert_or_assign("tags", std::move(tags));

    auto dom = to_toml(input);
    ASSERT_TRUE(dom.has_value());

    payload_with_extra output{};
    auto status = from_toml(*dom, output);
    ASSERT_TRUE(status.has_value());

    EXPECT_EQ(output.id, 1);
    auto city = output.extra["city"].value<std::string_view>();
    ASSERT_TRUE(city.has_value());
    EXPECT_EQ(*city, "shanghai");

    auto zip = output.extra["zip"].value<std::int64_t>();
    ASSERT_TRUE(zip.has_value());
    EXPECT_EQ(*zip, 200000);

    auto tags_out = output.extra["tags"].as_array();
    ASSERT_TRUE(tags_out != nullptr);
    ASSERT_EQ(tags_out->size(), 2U);
    EXPECT_EQ((*tags_out)[0].value<std::string_view>().value_or(""), "a");
    EXPECT_EQ((*tags_out)[1].value<std::string_view>().value_or(""), "b");
}

TEST_CASE(boxed_root_scalar_and_optional_none) {
    const std::vector<int> values{3, 5, 8};
    auto encoded_values = to_toml(values);
    ASSERT_TRUE(encoded_values.has_value());
    ASSERT_TRUE(encoded_values->contains("__value"));

    std::vector<int> decoded_values{};
    auto decode_values_status = from_toml(*encoded_values, decoded_values);
    ASSERT_TRUE(decode_values_status.has_value());
    EXPECT_EQ(decoded_values, values);

    const std::optional<int> none = std::nullopt;
    auto encoded_none = to_toml(none);
    ASSERT_TRUE(encoded_none.has_value());
    EXPECT_TRUE(encoded_none->empty());

    std::optional<int> decoded_none = 42;
    auto decode_none_status = from_toml(*encoded_none, decoded_none);
    ASSERT_TRUE(decode_none_status.has_value());
    EXPECT_FALSE(decoded_none.has_value());
}

TEST_CASE(tuple_length_errors) {
    // Helper: wrap a toml::array in a boxed root table (__value = arr)
    auto boxed = [](::toml::array arr) {
        ::toml::table tbl;
        tbl.insert_or_assign("__value", std::move(arr));
        return tbl;
    };

    // Too many elements for tuple<int,int>
    {
        auto tbl = boxed(::toml::array{1, 2, 3});
        std::tuple<int, int> t{};
        EXPECT_FALSE(from_toml(tbl, t).has_value());
    }

    // Too few elements for tuple<int,int>
    {
        auto tbl = boxed(::toml::array{1});
        std::tuple<int, int> t{};
        EXPECT_FALSE(from_toml(tbl, t).has_value());
    }

    // Too many elements for pair<int,int>
    {
        auto tbl = boxed(::toml::array{1, 2, 3});
        std::pair<int, int> p{};
        EXPECT_FALSE(from_toml(tbl, p).has_value());
    }

    // Too few elements for pair
    {
        auto tbl = boxed(::toml::array{1});
        std::pair<int, int> p{};
        EXPECT_FALSE(from_toml(tbl, p).has_value());
    }

    // Empty array into non-empty tuple
    {
        auto tbl = boxed(::toml::array{});
        std::tuple<int> t{};
        EXPECT_FALSE(from_toml(tbl, t).has_value());
    }

    // Non-empty array into empty tuple
    {
        auto tbl = boxed(::toml::array{1});
        std::tuple<> t{};
        EXPECT_FALSE(from_toml(tbl, t).has_value());
    }

    // Too many elements for array<int,2>
    {
        auto tbl = boxed(::toml::array{1, 2, 3});
        std::array<int, 2> a{};
        EXPECT_FALSE(from_toml(tbl, a).has_value());
    }

    // Too few elements for array<int,2>
    {
        auto tbl = boxed(::toml::array{1});
        std::array<int, 2> a{};
        EXPECT_FALSE(from_toml(tbl, a).has_value());
    }

    // Exact match still works
    {
        auto tbl = boxed(::toml::array{1, 2});
        std::tuple<int, int> t{};
        ASSERT_TRUE(from_toml(tbl, t).has_value());
        EXPECT_EQ(std::get<0>(t), 1);
        EXPECT_EQ(std::get<1>(t), 2);
    }

    // Type mismatch within tuple
    {
        auto tbl = boxed(::toml::array{1, "x"});
        std::tuple<int, int> t{};
        EXPECT_FALSE(from_toml(tbl, t).has_value());
    }
}

TEST_CASE(deserialize_to_content_value_scalars) {
    constexpr std::string_view input = R"(
name = "test"
age = 42
score = 3.14
active = true
)";

    auto result = toml::parse<content::Value>(input);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->is_object());

    const auto* obj = result->get_object();
    ASSERT_TRUE(obj != nullptr);

    const auto* name_val = obj->find("name");
    ASSERT_TRUE(name_val != nullptr);
    ASSERT_TRUE(name_val->is_string());
    EXPECT_EQ(name_val->as_string(), "test");

    const auto* age_val = obj->find("age");
    ASSERT_TRUE(age_val != nullptr);
    ASSERT_TRUE(age_val->is_int());
    EXPECT_EQ(age_val->as_int(), 42);

    const auto* score_val = obj->find("score");
    ASSERT_TRUE(score_val != nullptr);
    ASSERT_TRUE(score_val->is_number());
    EXPECT_EQ(score_val->as_double(), 3.14);

    const auto* active_val = obj->find("active");
    ASSERT_TRUE(active_val != nullptr);
    ASSERT_TRUE(active_val->is_bool());
    EXPECT_EQ(active_val->as_bool(), true);
}

TEST_CASE(deserialize_to_content_value_nested_table) {
    constexpr std::string_view input = R"(
title = "config"

[nested]
x = 1
y = 2

[nested.deep]
z = 3
)";

    auto result = toml::parse<content::Value>(input);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->is_object());

    const auto* root = result->get_object();
    ASSERT_TRUE(root != nullptr);

    const auto* title_val = root->find("title");
    ASSERT_TRUE(title_val != nullptr);
    EXPECT_EQ(title_val->as_string(), "config");

    const auto* nested_val = root->find("nested");
    ASSERT_TRUE(nested_val != nullptr);
    ASSERT_TRUE(nested_val->is_object());

    const auto* nested_obj = nested_val->get_object();
    ASSERT_TRUE(nested_obj != nullptr);

    const auto* x_val = nested_obj->find("x");
    ASSERT_TRUE(x_val != nullptr);
    EXPECT_EQ(x_val->as_int(), 1);

    const auto* y_val = nested_obj->find("y");
    ASSERT_TRUE(y_val != nullptr);
    EXPECT_EQ(y_val->as_int(), 2);

    const auto* deep_val = nested_obj->find("deep");
    ASSERT_TRUE(deep_val != nullptr);
    ASSERT_TRUE(deep_val->is_object());

    const auto* deep_obj = deep_val->get_object();
    ASSERT_TRUE(deep_obj != nullptr);

    const auto* z_val = deep_obj->find("z");
    ASSERT_TRUE(z_val != nullptr);
    EXPECT_EQ(z_val->as_int(), 3);
}

TEST_CASE(deserialize_to_content_value_arrays) {
    constexpr std::string_view input = R"(
items = [1, 2, 3]
tags = ["alpha", "beta"]
)";

    auto result = toml::parse<content::Value>(input);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->is_object());

    const auto* root = result->get_object();
    ASSERT_TRUE(root != nullptr);

    const auto* items_val = root->find("items");
    ASSERT_TRUE(items_val != nullptr);
    ASSERT_TRUE(items_val->is_array());

    const auto* items_arr = items_val->get_array();
    ASSERT_TRUE(items_arr != nullptr);
    ASSERT_EQ(items_arr->size(), 3U);
    EXPECT_EQ((*items_arr)[0].as_int(), 1);
    EXPECT_EQ((*items_arr)[1].as_int(), 2);
    EXPECT_EQ((*items_arr)[2].as_int(), 3);

    const auto* tags_val = root->find("tags");
    ASSERT_TRUE(tags_val != nullptr);
    ASSERT_TRUE(tags_val->is_array());

    const auto* tags_arr = tags_val->get_array();
    ASSERT_TRUE(tags_arr != nullptr);
    ASSERT_EQ(tags_arr->size(), 2U);
    EXPECT_EQ((*tags_arr)[0].as_string(), "alpha");
    EXPECT_EQ((*tags_arr)[1].as_string(), "beta");
}

TEST_CASE(deserialize_to_content_value_array_of_tables) {
    constexpr std::string_view input = R"(
[[people]]
name = "alice"
age = 30

[[people]]
name = "bob"
age = 25
)";

    auto result = toml::parse<content::Value>(input);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->is_object());

    const auto* root = result->get_object();
    ASSERT_TRUE(root != nullptr);

    const auto* people_val = root->find("people");
    ASSERT_TRUE(people_val != nullptr);
    ASSERT_TRUE(people_val->is_array());

    const auto* people_arr = people_val->get_array();
    ASSERT_TRUE(people_arr != nullptr);
    ASSERT_EQ(people_arr->size(), 2U);

    // First person
    ASSERT_TRUE((*people_arr)[0].is_object());
    const auto* person0 = (*people_arr)[0].get_object();
    ASSERT_TRUE(person0 != nullptr);
    const auto* p0_name = person0->find("name");
    ASSERT_TRUE(p0_name != nullptr);
    EXPECT_EQ(p0_name->as_string(), "alice");
    const auto* p0_age = person0->find("age");
    ASSERT_TRUE(p0_age != nullptr);
    EXPECT_EQ(p0_age->as_int(), 30);

    // Second person
    ASSERT_TRUE((*people_arr)[1].is_object());
    const auto* person1 = (*people_arr)[1].get_object();
    ASSERT_TRUE(person1 != nullptr);
    const auto* p1_name = person1->find("name");
    ASSERT_TRUE(p1_name != nullptr);
    EXPECT_EQ(p1_name->as_string(), "bob");
    const auto* p1_age = person1->find("age");
    ASSERT_TRUE(p1_age != nullptr);
    EXPECT_EQ(p1_age->as_int(), 25);
}

};  // TEST_SUITE(serde_toml)

}  // namespace

}  // namespace kota::codec

#endif
