#include <optional>
#include <utility>

#include "kota/zest/zest.h"
#include "kota/codec/content.h"

namespace kota::codec {

namespace {

TEST_SUITE(serde_content_serializer) {

TEST_CASE(dom_value_requires_closed_containers) {
    content::Serializer<> serializer;

    ASSERT_TRUE(serializer.begin_array(std::nullopt).has_value());
    ASSERT_TRUE(codec::serialize(serializer, 1).has_value());

    auto incomplete = serializer.dom_value();
    ASSERT_FALSE(incomplete.has_value());
    EXPECT_EQ(incomplete.error(), content::error_kind::invalid_state);

    ASSERT_TRUE(serializer.end_array().has_value());

    auto complete = serializer.dom_value();
    ASSERT_TRUE(complete.has_value());
    ASSERT_TRUE(complete->is_array());
    EXPECT_EQ(complete->as_array().size(), 1);
    EXPECT_EQ(complete->as_array()[0].as_int(), 1);
}

TEST_CASE(take_dom_value_moves_root_out) {
    content::Serializer<> serializer;

    ASSERT_TRUE(serializer.begin_object(2).has_value());
    ASSERT_TRUE(serializer.field("a").has_value());
    ASSERT_TRUE(codec::serialize(serializer, 1).has_value());
    ASSERT_TRUE(serializer.field("b").has_value());
    ASSERT_TRUE(codec::serialize(serializer, 2).has_value());
    ASSERT_TRUE(serializer.end_object().has_value());

    auto taken = serializer.take_dom_value();
    ASSERT_TRUE(taken.has_value());
    ASSERT_TRUE(taken->is_object());

    const auto& object = taken->as_object();
    EXPECT_EQ(object.size(), 2);
    EXPECT_EQ(object.at("a").as_int(), 1);
    EXPECT_EQ(object.at("b").as_int(), 2);
}

TEST_CASE(append_dom_value_inlines_foreign_subtree) {
    content::Serializer<> serializer;

    content::Object subtree;
    subtree.insert("k", content::Value(std::int64_t(9)));

    ASSERT_TRUE(serializer.begin_array(std::nullopt).has_value());
    ASSERT_TRUE(serializer.append_dom_value(std::move(subtree)).has_value());
    ASSERT_TRUE(serializer.end_array().has_value());

    auto dom = serializer.dom_value();
    ASSERT_TRUE(dom.has_value());
    ASSERT_TRUE(dom->is_array());
    const auto& array = dom->as_array();
    ASSERT_EQ(array.size(), 1);
    ASSERT_TRUE(array[0].is_object());
    EXPECT_EQ(array[0].as_object().at("k").as_int(), 9);
}

TEST_CASE(dom_value_before_any_write_returns_invalid_state) {
    content::Serializer<> serializer;

    auto result = serializer.dom_value();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), content::error_kind::invalid_state);

    auto take_result = serializer.take_dom_value();
    ASSERT_FALSE(take_result.has_value());
    EXPECT_EQ(take_result.error(), content::error_kind::invalid_state);
}

TEST_CASE(take_dom_value_resets_state) {
    content::Serializer<> serializer;

    ASSERT_TRUE(serializer.serialize_int(42).has_value());

    auto taken = serializer.take_dom_value();
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(taken->as_int(), 42);

    // After take, dom_value should report invalid_state (no root written)
    auto after = serializer.dom_value();
    ASSERT_FALSE(after.has_value());
    EXPECT_EQ(after.error(), content::error_kind::invalid_state);

    // Can write a new value and retrieve it
    ASSERT_TRUE(serializer.serialize_str("hello").has_value());
    auto second = serializer.dom_value();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->as_string(), "hello");
}

TEST_CASE(double_root_write_fails) {
    content::Serializer<> serializer;

    ASSERT_TRUE(serializer.serialize_int(1).has_value());
    auto result = serializer.serialize_int(2);
    ASSERT_FALSE(result.has_value());
    EXPECT_FALSE(serializer.valid());
}

};  // TEST_SUITE(serde_content_serializer)

}  // namespace

}  // namespace kota::codec
