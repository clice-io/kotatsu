#include <optional>

#include "eventide/zest/zest.h"
#include "eventide/serde/content.h"

namespace eventide::serde {

namespace {

TEST_SUITE(serde_content_serializer) {

TEST_CASE(dom_value_and_str_require_closed_containers) {
    content::Serializer<> serializer;

    auto seq = serializer.serialize_seq(std::nullopt);
    ASSERT_TRUE(seq.has_value());
    ASSERT_TRUE(seq->serialize_element(1).has_value());

    auto incomplete_dom = serializer.dom_value();
    ASSERT_FALSE(incomplete_dom.has_value());
    EXPECT_EQ(incomplete_dom.error(), content::error_kind::invalid_state);

    auto incomplete_str = serializer.str();
    ASSERT_FALSE(incomplete_str.has_value());
    EXPECT_EQ(incomplete_str.error(), content::error_kind::invalid_state);

    ASSERT_TRUE(seq->end().has_value());

    auto complete_dom = serializer.dom_value();
    ASSERT_TRUE(complete_dom.has_value());
    auto array = complete_dom->as_array();
    EXPECT_EQ(array.size(), std::size_t(1));
    EXPECT_EQ(array[0].as_int(), 1);

    auto complete_str = serializer.str();
    ASSERT_TRUE(complete_str.has_value());
    EXPECT_EQ(*complete_str, "[1]");
}

};  // TEST_SUITE(serde_content_serializer)

}  // namespace

}  // namespace eventide::serde
