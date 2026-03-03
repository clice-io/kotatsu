#if __has_include(<toml++/toml.hpp>)

#include <optional>
#include <string>
#include <vector>

#include "eventide/zest/zest.h"
#include "eventide/serde/toml.h"

namespace eventide::serde {

namespace {

using toml::from_toml;
using toml::parse;
using toml::to_string;
using toml::to_toml;

struct person {
    int id = 0;
    std::string name;
    std::vector<int> scores;
    bool active = false;

    auto operator==(const person&) const -> bool = default;
};

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

};  // TEST_SUITE(serde_toml)

}  // namespace

}  // namespace eventide::serde

#endif
