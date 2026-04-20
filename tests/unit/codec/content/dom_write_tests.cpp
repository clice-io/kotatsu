#include <cstdint>
#include <string>
#include <utility>

#include "kota/zest/zest.h"
#include "kota/codec/content.h"

namespace kota::codec {

namespace {

TEST_SUITE(serde_content_dom_write) {

TEST_CASE(value_reassignment_changes_kind) {
    content::Value value(std::int64_t(1));
    ASSERT_TRUE(value.is_int());

    value = content::Value("x");
    ASSERT_TRUE(value.is_string());
    EXPECT_EQ(value.as_string(), "x");

    content::Array arr;
    arr.push_back(content::Value(std::int64_t(2)));
    value = content::Value(std::move(arr));
    ASSERT_TRUE(value.is_array());
    EXPECT_EQ(value.as_array().size(), 1);
    EXPECT_EQ(value.as_array()[0].as_int(), 2);
}

TEST_CASE(array_push_back_and_emplace_back) {
    content::Array array;
    array.push_back(content::Value(nullptr));
    array.push_back(content::Value(true));
    array.emplace_back(std::int64_t(7));
    array.emplace_back("z");

    ASSERT_EQ(array.size(), 4);
    EXPECT_TRUE(array[0].is_null());
    EXPECT_EQ(array[1].as_bool(), true);
    EXPECT_EQ(array[2].as_int(), 7);
    EXPECT_EQ(array[3].as_string(), "z");
}

TEST_CASE(array_clear_and_reserve) {
    content::Array array;
    array.reserve(4);
    array.push_back(content::Value(std::int64_t(1)));
    array.push_back(content::Value(std::int64_t(2)));
    ASSERT_EQ(array.size(), 2);

    array.clear();
    EXPECT_TRUE(array.empty());
    EXPECT_EQ(array.size(), 0);
}

TEST_CASE(object_assign_is_upsert) {
    content::Object object;
    object.assign("a", content::Value(std::int64_t(1)));
    object.assign("a", content::Value(std::int64_t(2)));
    object.assign("b", content::Value(std::int64_t(3)));

    EXPECT_EQ(object.size(), 2);
    EXPECT_EQ(object.at("a").as_int(), 2);
    EXPECT_EQ(object.at("b").as_int(), 3);
}

TEST_CASE(object_insert_appends_preserving_duplicates) {
    content::Object object;
    object.insert("k", content::Value(std::int64_t(1)));
    object.insert("k", content::Value(std::int64_t(2)));

    EXPECT_EQ(object.size(), 2);
    EXPECT_EQ(object.begin()[0].value.as_int(), 1);
    EXPECT_EQ(object.begin()[1].value.as_int(), 2);
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

    EXPECT_EQ(object.remove("a"), 2);
    EXPECT_EQ(object.remove("a"), 0);
    EXPECT_FALSE(object.contains("a"));
    EXPECT_TRUE(object.contains("b"));
    EXPECT_EQ(object.size(), 1);
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

TEST_CASE(object_assign_existing_key_preserves_index_correctness) {
    content::Object object;
    object.insert("a", content::Value(std::int64_t(1)));
    object.insert("b", content::Value(std::int64_t(2)));
    object.insert("c", content::Value(std::int64_t(3)));

    // Trigger index build
    EXPECT_EQ(object.find("b")->as_int(), 2);

    // Assign existing key — should NOT invalidate index
    object.assign("b", content::Value(std::int64_t(20)));

    // All lookups still work correctly
    EXPECT_EQ(object.find("a")->as_int(), 1);
    EXPECT_EQ(object.find("b")->as_int(), 20);
    EXPECT_EQ(object.find("c")->as_int(), 3);
    EXPECT_EQ(object.size(), 3);
}

TEST_CASE(object_remove_then_insert_same_key) {
    content::Object object;
    object.insert("x", content::Value(std::int64_t(1)));
    object.insert("y", content::Value(std::int64_t(2)));

    // Trigger index build
    EXPECT_EQ(object.find("x")->as_int(), 1);

    // Remove and re-insert
    EXPECT_EQ(object.remove("x"), 1);
    EXPECT_EQ(object.find("x"), nullptr);

    object.insert("x", content::Value(std::int64_t(99)));
    ASSERT_NE(object.find("x"), nullptr);
    EXPECT_EQ(object.find("x")->as_int(), 99);
    EXPECT_EQ(object.find("y")->as_int(), 2);
}

TEST_CASE(object_clear_then_lookup) {
    content::Object object;
    object.insert("a", content::Value(std::int64_t(1)));
    object.insert("b", content::Value(std::int64_t(2)));

    // Trigger index build
    EXPECT_EQ(object.find("a")->as_int(), 1);

    object.clear();
    EXPECT_TRUE(object.empty());
    EXPECT_EQ(object.find("a"), nullptr);
    EXPECT_FALSE(object.contains("b"));
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

TEST_CASE(small_object_lookup_without_index) {
    // Objects with <= 8 entries should work correctly via linear scan.
    content::Object obj;
    for(int i = 0; i < 8; ++i) {
        obj.insert("k" + std::to_string(i), content::Value(std::int64_t(i)));
    }

    // All lookups work
    for(int i = 0; i < 8; ++i) {
        auto* v = obj.find("k" + std::to_string(i));
        ASSERT_NE(v, nullptr);
        EXPECT_EQ(v->as_int(), i);
    }
    EXPECT_EQ(obj.find("missing"), nullptr);
}

TEST_CASE(large_object_builds_index_on_lookup) {
    // Objects with > 8 entries should build an index.
    content::Object obj;
    for(int i = 0; i < 20; ++i) {
        obj.insert("k" + std::to_string(i), content::Value(std::int64_t(i)));
    }

    // First lookup triggers index build; all entries remain accessible.
    for(int i = 0; i < 20; ++i) {
        auto* v = obj.find("k" + std::to_string(i));
        ASSERT_NE(v, nullptr);
        EXPECT_EQ(v->as_int(), i);
    }
    EXPECT_EQ(obj.find("missing"), nullptr);
}

TEST_CASE(insert_after_index_build_invalidates_and_rebuilds) {
    content::Object obj;
    for(int i = 0; i < 10; ++i) {
        obj.insert("k" + std::to_string(i), content::Value(std::int64_t(i)));
    }

    // Trigger index build
    EXPECT_EQ(obj.find("k5")->as_int(), 5);

    // Insert invalidates index; next lookup rebuilds correctly.
    obj.insert("new", content::Value(std::int64_t(99)));
    EXPECT_EQ(obj.find("new")->as_int(), 99);
    EXPECT_EQ(obj.find("k0")->as_int(), 0);
    EXPECT_EQ(obj.find("k9")->as_int(), 9);
}

TEST_CASE(many_inserts_with_reallocation_stays_correct) {
    content::Object obj;

    // Insert enough entries to trigger multiple vector reallocations.
    for(int i = 0; i < 100; ++i) {
        obj.insert("key" + std::to_string(i), content::Value(std::int64_t(i)));
        // Interleave lookups to trigger index build/invalidate cycles.
        if(i % 15 == 0 && i > 0) {
            EXPECT_EQ(obj.find("key0")->as_int(), 0);
            EXPECT_EQ(obj.find("key" + std::to_string(i))->as_int(), i);
        }
    }

    // Final verification
    for(int i = 0; i < 100; ++i) {
        auto* v = obj.find("key" + std::to_string(i));
        ASSERT_NE(v, nullptr);
        EXPECT_EQ(v->as_int(), i);
    }
}

TEST_CASE(remove_from_large_object_invalidates_index) {
    content::Object obj;
    for(int i = 0; i < 12; ++i) {
        obj.insert("k" + std::to_string(i), content::Value(std::int64_t(i)));
    }

    // Build index
    EXPECT_EQ(obj.find("k5")->as_int(), 5);

    // Remove middle element
    EXPECT_EQ(obj.remove("k5"), 1);
    EXPECT_EQ(obj.find("k5"), nullptr);

    // Other elements still found correctly after index rebuild
    EXPECT_EQ(obj.find("k0")->as_int(), 0);
    EXPECT_EQ(obj.find("k11")->as_int(), 11);
    EXPECT_EQ(obj.size(), 11);
}

TEST_CASE(duplicate_keys_find_returns_last_inserted) {
    content::Object obj;
    for(int i = 0; i < 10; ++i) {
        obj.insert("dup", content::Value(std::int64_t(i)));
    }
    // find should return the last-inserted entry (index 9)
    ASSERT_NE(obj.find("dup"), nullptr);
    EXPECT_EQ(obj.find("dup")->as_int(), 9);
}

};  // TEST_SUITE(serde_content_dom_write)

}  // namespace

}  // namespace kota::codec
