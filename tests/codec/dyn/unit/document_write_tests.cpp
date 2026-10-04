#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "kota/zest/zest.h"
#include "kota/codec/dyn/document.h"

namespace kota::codec {

namespace {

/// An object scans for a key up to 16 entries and looks it up through an
/// index above that, built by the first lookup. The lookup cases run at a
/// size on each side.
constexpr std::array<int, 2> sizes{8, 32};

std::string key(int i) {
    return std::format("k{}", i);
}

/// k0: 0, k1: 1, ...
dyn::Object numbered(int count) {
    dyn::Object object;
    for(int i = 0; i < count; ++i) {
        object.insert(key(i), std::int64_t{i});
    }
    return object;
}

/// The integer under `name`, if there is one.
std::optional<std::int64_t> lookup(const dyn::Object& object, std::string_view name) {
    const auto* value = object.find(name);
    return value ? value->get_int() : std::nullopt;
}

ZEST_SUITE(codec_dyn_document_write) {

ZEST_CASE(assignment_changes_kind) {
    dyn::Value value(std::int64_t{1});
    value = dyn::Value("x");
    ZEXPECT(value == dyn::Value("x"));
    value = dyn::Value(dyn::Array{std::int64_t{2}});
    ZEXPECT(value == dyn::Value(dyn::Array{std::int64_t{2}}));
}

ZEST_CASE(array_push_back_and_emplace_back) {
    dyn::Array array;
    array.push_back(dyn::Value(nullptr));
    array.push_back(dyn::Value(true));
    array.emplace_back(std::int64_t{7});
    array.emplace_back("z");
    ZEXPECT(array == (dyn::Array{nullptr, true, std::int64_t{7}, "z"}));
}

ZEST_CASE(array_clear_and_reserve) {
    dyn::Array array;
    array.reserve(4);
    array.push_back(std::int64_t{1});
    array.push_back(std::int64_t{2});
    ZEXPECT(array.size() == 2U);

    array.clear();
    ZEXPECT(array.empty());
    ZEXPECT(array.size() == 0U);
}

ZEST_CASE(array_elements_mutable_through_iteration) {
    dyn::Array array{std::int64_t{1}, std::int64_t{2}, std::int64_t{3}};
    for(auto& value: array) {
        value = dyn::Value(value.as_int() * 10);
    }
    ZEXPECT(array == (dyn::Array{std::int64_t{10}, std::int64_t{20}, std::int64_t{30}}));
}

ZEST_CASE(object_values_mutable_through_iteration) {
    dyn::Object object;
    object.insert("a", std::int64_t{1});
    object.insert("b", std::int64_t{2});
    for(auto& [name, value]: object) {
        value = dyn::Value(value.as_int() + 100);
    }
    ZEXPECT(lookup(object, "a") == std::int64_t{101});
    ZEXPECT(lookup(object, "b") == std::int64_t{102});
}

ZEST_CASE(object_assign_is_upsert) {
    dyn::Object object;
    object.assign("a", std::int64_t{1});
    object.assign("a", std::int64_t{2});
    object.assign("b", std::int64_t{3});
    ZEXPECT(object.size() == 2U);
    ZEXPECT(lookup(object, "a") == std::int64_t{2});
    ZEXPECT(lookup(object, "b") == std::int64_t{3});
}

ZEST_CASE(object_insert_keeps_duplicates) {
    dyn::Object object;
    object.insert("k", std::int64_t{1});
    object.insert("k", std::int64_t{2});
    ZASSERT(object.size() == 2U);
    ZEXPECT(object.begin()[0].second == dyn::Value(std::int64_t{1}));
    ZEXPECT(object.begin()[1].second == dyn::Value(std::int64_t{2}));
}

ZEST_CASE(object_find_returns_last_duplicate) {
    for(int size: sizes) {
        ZEST_CONTEXT("{} duplicates", size);
        dyn::Object object;
        for(int i = 0; i < size; ++i) {
            object.insert("dup", std::int64_t{i});
        }
        ZEXPECT(object.contains("dup"));
        ZEXPECT(lookup(object, "dup") == std::int64_t{size - 1});
    }
}

ZEST_CASE(object_remove_erases_every_match) {
    dyn::Object object;
    object.assign("a", std::int64_t{1});
    object.assign("b", std::int64_t{2});
    object.insert("a", std::int64_t{11});

    ZEXPECT(object.remove("a") == 2U);
    ZEXPECT(object.remove("a") == 0U);
    ZEXPECT(!object.contains("a"));
    ZEXPECT(object.contains("b"));
    ZEXPECT(object.size() == 1U);
}

ZEST_CASE(lookup_finds_every_key) {
    for(int size: sizes) {
        ZEST_CONTEXT("{} entries", size);
        auto object = numbered(size);
        for(int i = 0; i < size; ++i) {
            ZEST_CONTEXT("key {}", key(i));
            ZEXPECT(lookup(object, key(i)) == std::int64_t{i});
        }
        ZEXPECT(object.find("missing") == nullptr);
        ZEXPECT(!object.contains("missing"));
    }
}

ZEST_CASE(insert_after_lookup_is_found) {
    for(int size: sizes) {
        ZEST_CONTEXT("{} entries", size);
        auto object = numbered(size);
        ZEXPECT(lookup(object, "k5") == std::int64_t{5});
        object.insert("new", std::int64_t{99});
        ZEXPECT(lookup(object, "new") == std::int64_t{99});
        ZEXPECT(lookup(object, "k0") == std::int64_t{0});
        ZEXPECT(lookup(object, key(size - 1)) == std::int64_t{size - 1});
    }
}

ZEST_CASE(assign_after_lookup_is_found) {
    for(int size: sizes) {
        ZEST_CONTEXT("{} entries", size);
        auto object = numbered(size);
        ZEXPECT(lookup(object, "k5") == std::int64_t{5});
        // An existing key's value is replaced in place, and the lookups
        // after it go through the index the first one built.
        object.assign("k5", std::int64_t{50});
        ZEXPECT(lookup(object, "k5") == std::int64_t{50});
        ZEXPECT(lookup(object, "k0") == std::int64_t{0});
        ZEXPECT(lookup(object, key(size - 1)) == std::int64_t{size - 1});
        ZEXPECT(object.size() == static_cast<std::size_t>(size));
        object.assign("new", std::int64_t{99});
        ZEXPECT(lookup(object, "new") == std::int64_t{99});
        ZEXPECT(lookup(object, "k5") == std::int64_t{50});
        ZEXPECT(object.size() == static_cast<std::size_t>(size + 1));
    }
}

ZEST_CASE(remove_after_lookup_is_missed) {
    for(int size: sizes) {
        ZEST_CONTEXT("{} entries", size);
        auto object = numbered(size);
        ZEXPECT(lookup(object, "k5") == std::int64_t{5});
        ZEXPECT(object.remove("k5") == 1U);
        ZEXPECT(object.find("k5") == nullptr);
        ZEXPECT(lookup(object, "k0") == std::int64_t{0});
        ZEXPECT(lookup(object, key(size - 1)) == std::int64_t{size - 1});

        object.insert("k5", std::int64_t{99});
        ZEXPECT(lookup(object, "k5") == std::int64_t{99});
    }
}

ZEST_CASE(clear_after_lookup_misses_every_key) {
    for(int size: sizes) {
        ZEST_CONTEXT("{} entries", size);
        auto object = numbered(size);
        ZEXPECT(lookup(object, "k0") == std::int64_t{0});
        object.clear();
        ZEXPECT(object.empty());
        ZEXPECT(object.find("k0") == nullptr);
    }
}

ZEST_CASE(lookups_stay_correct_while_growing) {
    // Lookups between inserts cross the index threshold and rebuild the
    // index as the entries move to new storage.
    dyn::Object object;
    for(int i = 0; i < 100; ++i) {
        object.insert(key(i), std::int64_t{i});
        if(i % 15 == 0) {
            ZEST_CONTEXT("after {} inserts", i + 1);
            ZEXPECT(lookup(object, "k0") == std::int64_t{0});
            ZEXPECT(lookup(object, key(i)) == std::int64_t{i});
        }
    }
    for(int i = 0; i < 100; ++i) {
        ZEST_CONTEXT("key {}", key(i));
        ZEXPECT(lookup(object, key(i)) == std::int64_t{i});
    }
}

};  // ZEST_SUITE(codec_dyn_document_write)

}  // namespace

}  // namespace kota::codec
