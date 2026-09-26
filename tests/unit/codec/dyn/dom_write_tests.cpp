#include <cstdint>
#include <string>
#include <utility>

#include "kota/zest/zest.h"
#include "kota/codec/dyn/dyn.h"

namespace kota::codec {

namespace {

ZEST_SUITE(serde_content_dom_write){

    ZEST_CASE(value_reassignment_changes_kind){dyn::Value value(std::int64_t(1));
ASSERT(value.is_int());

value = dyn::Value("x");
ASSERT(value.is_string());
EXPECT(value.as_string() == "x");

dyn::Array arr;
arr.push_back(dyn::Value(std::int64_t(2)));
value = dyn::Value(std::move(arr));
ASSERT(value.is_array());
EXPECT(value.as_array().size() == 1);
EXPECT(value.as_array()[0].as_int() == 2);

}  // namespace

ZEST_CASE(array_push_back_and_emplace_back) {
    dyn::Array array;
    array.push_back(dyn::Value(nullptr));
    array.push_back(dyn::Value(true));
    array.emplace_back(std::int64_t(7));
    array.emplace_back("z");

    ASSERT(array.size() == 4);
    EXPECT(array[0].is_null());
    EXPECT(array[1].as_bool() == true);
    EXPECT(array[2].as_int() == 7);
    EXPECT(array[3].as_string() == "z");
}

ZEST_CASE(array_clear_and_reserve) {
    dyn::Array array;
    array.reserve(4);
    array.push_back(dyn::Value(std::int64_t(1)));
    array.push_back(dyn::Value(std::int64_t(2)));
    ASSERT(array.size() == 2);

    array.clear();
    EXPECT(array.empty());
    EXPECT(array.size() == 0);
}

ZEST_CASE(object_assign_is_upsert) {
    dyn::Object object;
    object.assign("a", dyn::Value(std::int64_t(1)));
    object.assign("a", dyn::Value(std::int64_t(2)));
    object.assign("b", dyn::Value(std::int64_t(3)));

    EXPECT(object.size() == 2);
    EXPECT(object.at("a").as_int() == 2);
    EXPECT(object.at("b").as_int() == 3);
}

ZEST_CASE(object_insert_appends_preserving_duplicates) {
    dyn::Object object;
    object.insert("k", dyn::Value(std::int64_t(1)));
    object.insert("k", dyn::Value(std::int64_t(2)));

    EXPECT(object.size() == 2);
    EXPECT(object.begin()[0].second.as_int() == 1);
    EXPECT(object.begin()[1].second.as_int() == 2);
}

ZEST_CASE(object_find_returns_latest_when_duplicates) {
    dyn::Object object;
    object.insert("k", dyn::Value(std::int64_t(1)));
    object.insert("k", dyn::Value(std::int64_t(2)));
    object.insert("k", dyn::Value(std::int64_t(3)));

    ASSERT(object.contains("k"));
    ASSERT(object.find("k") != nullptr);
    EXPECT(object.find("k")->as_int() == 3);
    EXPECT(object.at("k").as_int() == 3);
}

ZEST_CASE(object_find_returns_nullptr_when_missing) {
    dyn::Object object;
    object.insert("present", dyn::Value(std::int64_t(1)));

    EXPECT(object.find("present")->as_int() == 1);
    EXPECT(object.find("absent") == nullptr);
    EXPECT(object.contains("present"));
    EXPECT(!object.contains("absent"));
}

ZEST_CASE(object_remove_erases_all_matching_and_returns_count) {
    dyn::Object object;
    object.assign("a", dyn::Value(std::int64_t(1)));
    object.assign("b", dyn::Value(std::int64_t(2)));
    object.insert("a", dyn::Value(std::int64_t(11)));

    EXPECT(object.remove("a") == 2);
    EXPECT(object.remove("a") == 0);
    EXPECT(!object.contains("a"));
    EXPECT(object.contains("b"));
    EXPECT(object.size() == 1);
}

ZEST_CASE(object_lookup_reflects_mutations) {
    dyn::Object object;
    for(int i = 0; i < 8; ++i) {
        object.insert("k" + std::to_string(i), dyn::Value(std::int64_t(i)));
    }

    EXPECT(object.at("k3").as_int() == 3);

    object.remove("k3");
    EXPECT(object.find("k3") == nullptr);
    EXPECT(object.at("k4").as_int() == 4);

    object.assign("k4", dyn::Value(std::int64_t(40)));
    EXPECT(object.at("k4").as_int() == 40);

    object.insert("k9", dyn::Value(std::int64_t(9)));
    EXPECT(object.at("k9").as_int() == 9);
}

ZEST_CASE(object_index_invalidated_after_cached_lookup_then_insert) {
    dyn::Object object;
    object.insert("a", dyn::Value(std::int64_t(1)));
    object.insert("b", dyn::Value(std::int64_t(2)));

    EXPECT(object.at("a").as_int() == 1);

    object.insert("c", dyn::Value(std::int64_t(3)));
    EXPECT(object.at("c").as_int() == 3);
    EXPECT(object.at("a").as_int() == 1);
}

ZEST_CASE(object_index_invalidated_after_cached_lookup_then_assign_new_key) {
    dyn::Object object;
    object.insert("a", dyn::Value(std::int64_t(1)));

    EXPECT(object.at("a").as_int() == 1);

    object.assign("b", dyn::Value(std::int64_t(2)));
    EXPECT(object.at("b").as_int() == 2);
    EXPECT(object.at("a").as_int() == 1);
}

ZEST_CASE(object_assign_existing_key_preserves_index_correctness) {
    dyn::Object object;
    // Seed > 16 entries to cross the indexing threshold.
    for(int i = 0; i < 20; ++i) {
        object.insert("k" + std::to_string(i), dyn::Value(std::int64_t(i)));
    }

    // Trigger index build
    EXPECT(object.find("k5")->as_int() == 5);

    // Assign existing key — should NOT invalidate index
    object.assign("k5", dyn::Value(std::int64_t(50)));

    // All lookups still work correctly via cached index
    EXPECT(object.find("k0")->as_int() == 0);
    EXPECT(object.find("k5")->as_int() == 50);
    EXPECT(object.find("k19")->as_int() == 19);
    EXPECT(object.size() == 20);
}

ZEST_CASE(object_remove_then_insert_same_key) {
    dyn::Object object;
    object.insert("x", dyn::Value(std::int64_t(1)));
    object.insert("y", dyn::Value(std::int64_t(2)));

    // Trigger index build
    EXPECT(object.find("x")->as_int() == 1);

    // Remove and re-insert
    EXPECT(object.remove("x") == 1);
    EXPECT(object.find("x") == nullptr);

    object.insert("x", dyn::Value(std::int64_t(99)));
    ASSERT(object.find("x") != nullptr);
    EXPECT(object.find("x")->as_int() == 99);
    EXPECT(object.find("y")->as_int() == 2);
}

ZEST_CASE(object_clear_then_lookup) {
    dyn::Object object;
    object.insert("a", dyn::Value(std::int64_t(1)));
    object.insert("b", dyn::Value(std::int64_t(2)));

    // Trigger index build
    EXPECT(object.find("a")->as_int() == 1);

    object.clear();
    EXPECT(object.empty());
    EXPECT(object.find("a") == nullptr);
    EXPECT(!object.contains("b"));
}

ZEST_CASE(object_equality_multiset_with_duplicates) {
    dyn::Object a;
    a.insert("k", dyn::Value(std::int64_t(1)));
    a.insert("k", dyn::Value(std::int64_t(2)));

    dyn::Object b;
    b.insert("k", dyn::Value(std::int64_t(2)));
    b.insert("k", dyn::Value(std::int64_t(1)));

    EXPECT(a == b);

    dyn::Object c;
    c.insert("k", dyn::Value(std::int64_t(1)));
    c.insert("k", dyn::Value(std::int64_t(1)));
    EXPECT(a != c);

    dyn::Object d;
    d.insert("k", dyn::Value(std::int64_t(1)));
    EXPECT(a != d);
}

ZEST_CASE(small_object_lookup_without_index) {
    // Objects with <= 8 entries should work correctly via linear scan.
    dyn::Object obj;
    for(int i = 0; i < 8; ++i) {
        obj.insert("k" + std::to_string(i), dyn::Value(std::int64_t(i)));
    }

    // All lookups work
    for(int i = 0; i < 8; ++i) {
        auto* v = obj.find("k" + std::to_string(i));
        ASSERT(v != nullptr);
        EXPECT(v->as_int() == i);
    }
    EXPECT(obj.find("missing") == nullptr);
}

ZEST_CASE(large_object_builds_index_on_lookup) {
    // Objects with > 8 entries should build an index.
    dyn::Object obj;
    for(int i = 0; i < 20; ++i) {
        obj.insert("k" + std::to_string(i), dyn::Value(std::int64_t(i)));
    }

    // First lookup triggers index build; all entries remain accessible.
    for(int i = 0; i < 20; ++i) {
        auto* v = obj.find("k" + std::to_string(i));
        ASSERT(v != nullptr);
        EXPECT(v->as_int() == i);
    }
    EXPECT(obj.find("missing") == nullptr);
}

ZEST_CASE(insert_after_index_build_invalidates_and_rebuilds) {
    dyn::Object obj;
    for(int i = 0; i < 10; ++i) {
        obj.insert("k" + std::to_string(i), dyn::Value(std::int64_t(i)));
    }

    // Trigger index build
    EXPECT(obj.find("k5")->as_int() == 5);

    // Insert invalidates index; next lookup rebuilds correctly.
    obj.insert("new", dyn::Value(std::int64_t(99)));
    EXPECT(obj.find("new")->as_int() == 99);
    EXPECT(obj.find("k0")->as_int() == 0);
    EXPECT(obj.find("k9")->as_int() == 9);
}

ZEST_CASE(many_inserts_with_reallocation_stays_correct) {
    dyn::Object obj;

    // Insert enough entries to trigger multiple vector reallocations.
    for(int i = 0; i < 100; ++i) {
        obj.insert("key" + std::to_string(i), dyn::Value(std::int64_t(i)));
        // Interleave lookups to trigger index build/invalidate cycles.
        if(i % 15 == 0 && i > 0) {
            EXPECT(obj.find("key0")->as_int() == 0);
            EXPECT(obj.find("key" + std::to_string(i))->as_int() == i);
        }
    }

    // Final verification
    for(int i = 0; i < 100; ++i) {
        auto* v = obj.find("key" + std::to_string(i));
        ASSERT(v != nullptr);
        EXPECT(v->as_int() == i);
    }
}

ZEST_CASE(remove_from_large_object_invalidates_index) {
    dyn::Object obj;
    for(int i = 0; i < 12; ++i) {
        obj.insert("k" + std::to_string(i), dyn::Value(std::int64_t(i)));
    }

    // Build index
    EXPECT(obj.find("k5")->as_int() == 5);

    // Remove middle element
    EXPECT(obj.remove("k5") == 1);
    EXPECT(obj.find("k5") == nullptr);

    // Other elements still found correctly after index rebuild
    EXPECT(obj.find("k0")->as_int() == 0);
    EXPECT(obj.find("k11")->as_int() == 11);
    EXPECT(obj.size() == 11);
}

ZEST_CASE(duplicate_keys_find_returns_last_inserted) {
    dyn::Object obj;
    for(int i = 0; i < 10; ++i) {
        obj.insert("dup", dyn::Value(std::int64_t(i)));
    }
    // find should return the last-inserted entry (index 9)
    ASSERT(obj.find("dup") != nullptr);
    EXPECT(obj.find("dup")->as_int() == 9);
}

};  // namespace kota::codec

}  // namespace

}  // namespace kota::codec
