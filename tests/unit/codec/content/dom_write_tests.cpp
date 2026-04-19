#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "kota/zest/zest.h"
#include "kota/codec/content.h"

namespace kota::codec {

namespace {

TEST_SUITE(serde_content_dom_write) {

TEST_CASE(value_reassignment_changes_kind) {
    content::Value value(std::int64_t(1));
    ASSERT_TRUE(value.is_int());

    value = content::Value(std::string_view("x"));
    ASSERT_TRUE(value.is_string());
    EXPECT_EQ(value.as_string(), std::string_view("x"));

    content::Array arr;
    arr.push_back(content::Value(std::int64_t(2)));
    value = content::Value(std::move(arr));
    ASSERT_TRUE(value.is_array());
    EXPECT_EQ(value.as_array().size(), std::size_t(1));
    EXPECT_EQ(value.as_array()[0].as_int(), 2);
}

TEST_CASE(array_push_back_and_emplace_back) {
    content::Array array;
    array.push_back(content::Value(nullptr));
    array.push_back(content::Value(true));
    array.emplace_back(std::int64_t(7));
    array.emplace_back(std::string("z"));

    ASSERT_EQ(array.size(), std::size_t(4));
    EXPECT_TRUE(array[0].is_null());
    EXPECT_EQ(array[1].as_bool(), true);
    EXPECT_EQ(array[2].as_int(), 7);
    EXPECT_EQ(array[3].as_string(), std::string_view("z"));
}

TEST_CASE(array_clear_and_reserve) {
    content::Array array;
    array.reserve(4);
    array.push_back(content::Value(std::int64_t(1)));
    array.push_back(content::Value(std::int64_t(2)));
    ASSERT_EQ(array.size(), std::size_t(2));

    array.clear();
    EXPECT_TRUE(array.empty());
    EXPECT_EQ(array.size(), std::size_t(0));
}

TEST_CASE(object_assign_is_upsert) {
    content::Object object;
    object.assign("a", content::Value(std::int64_t(1)));
    object.assign("a", content::Value(std::int64_t(2)));
    object.assign("b", content::Value(std::int64_t(3)));

    EXPECT_EQ(object.size(), std::size_t(2));
    EXPECT_EQ(object.at("a").as_int(), 2);
    EXPECT_EQ(object.at("b").as_int(), 3);
}

TEST_CASE(object_insert_appends_preserving_duplicates) {
    content::Object object;
    object.insert("k", content::Value(std::int64_t(1)));
    object.insert("k", content::Value(std::int64_t(2)));

    EXPECT_EQ(object.size(), std::size_t(2));
    EXPECT_EQ(object.entries()[0].value.as_int(), 1);
    EXPECT_EQ(object.entries()[1].value.as_int(), 2);
}

TEST_CASE(object_find_returns_latest_when_duplicates) {
    content::Object object;
    object.insert("k", content::Value(std::int64_t(1)));
    object.insert("k", content::Value(std::int64_t(2)));
    object.insert("k", content::Value(std::int64_t(3)));

    ASSERT_TRUE(object.contains("k"));
    ASSERT_NE(object.find("k"), nullptr);
    EXPECT_EQ(object.find("k")->as_int(), 3);
    EXPECT_EQ(object.at("k").as_int(), 3);
}

TEST_CASE(object_find_returns_nullptr_when_missing) {
    content::Object object;
    object.insert("present", content::Value(std::int64_t(1)));

    EXPECT_EQ(object.find("present")->as_int(), 1);
    EXPECT_EQ(object.find("absent"), nullptr);
    EXPECT_TRUE(object.contains("present"));
    EXPECT_FALSE(object.contains("absent"));
}

TEST_CASE(object_remove_erases_all_matching_and_returns_count) {
    content::Object object;
    object.assign("a", content::Value(std::int64_t(1)));
    object.assign("b", content::Value(std::int64_t(2)));
    object.insert("a", content::Value(std::int64_t(11)));

    EXPECT_EQ(object.remove("a"), std::size_t(2));
    EXPECT_EQ(object.remove("a"), std::size_t(0));
    EXPECT_FALSE(object.contains("a"));
    EXPECT_TRUE(object.contains("b"));
    EXPECT_EQ(object.size(), std::size_t(1));
}

TEST_CASE(object_lookup_reflects_mutations) {
    content::Object object;
    for(int i = 0; i < 8; ++i) {
        object.insert("k" + std::to_string(i), content::Value(std::int64_t(i)));
    }

    EXPECT_EQ(object.at("k3").as_int(), 3);

    object.remove("k3");
    EXPECT_EQ(object.find("k3"), nullptr);
    EXPECT_EQ(object.at("k4").as_int(), 4);

    object.assign("k4", content::Value(std::int64_t(40)));
    EXPECT_EQ(object.at("k4").as_int(), 40);

    object.insert("k9", content::Value(std::int64_t(9)));
    EXPECT_EQ(object.at("k9").as_int(), 9);
}

TEST_CASE(object_index_invalidated_after_cached_lookup_then_insert) {
    content::Object object;
    object.insert("a", content::Value(std::int64_t(1)));
    object.insert("b", content::Value(std::int64_t(2)));

    EXPECT_EQ(object.at("a").as_int(), 1);

    object.insert("c", content::Value(std::int64_t(3)));
    EXPECT_EQ(object.at("c").as_int(), 3);
    EXPECT_EQ(object.at("a").as_int(), 1);
}

TEST_CASE(object_index_invalidated_after_cached_lookup_then_assign_new_key) {
    content::Object object;
    object.insert("a", content::Value(std::int64_t(1)));

    EXPECT_EQ(object.at("a").as_int(), 1);

    object.assign("b", content::Value(std::int64_t(2)));
    EXPECT_EQ(object.at("b").as_int(), 2);
    EXPECT_EQ(object.at("a").as_int(), 1);
}

TEST_CASE(object_equality_multiset_with_duplicates) {
    content::Object a;
    a.insert("k", content::Value(std::int64_t(1)));
    a.insert("k", content::Value(std::int64_t(2)));

    content::Object b;
    b.insert("k", content::Value(std::int64_t(2)));
    b.insert("k", content::Value(std::int64_t(1)));

    EXPECT_TRUE(a == b);

    content::Object c;
    c.insert("k", content::Value(std::int64_t(1)));
    c.insert("k", content::Value(std::int64_t(1)));
    EXPECT_FALSE(a == c);

    content::Object d;
    d.insert("k", content::Value(std::int64_t(1)));
    EXPECT_FALSE(a == d);
}

};  // TEST_SUITE(serde_content_dom_write)

}  // namespace

}  // namespace kota::codec
