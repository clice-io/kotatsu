// Regression test: accessing virtual_schema<T>::fields for recursive types must work
// even when it is the FIRST interaction with the type_info system for that type in the TU.
//
// Background: build_fields<T>() stores type_info_of<child> function pointers in field_info.
// For recursive types this creates a transitive instantiation chain back to
// type_instance<T>::value, whose initializer references fields.data().  If `fields` is
// evaluated directly (not through `value`), Clang detects a circular dependency.
// The fix routes virtual_schema::fields through type_info_of<T>() (i.e. through `value`),
// so `fields` is always resolved as a sub-expression of `value`, avoiding re-entry.

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "fixtures/schema/recursive.h"
#include "kota/zest/zest.h"
#include "kota/meta/schema.h"

namespace kota::meta {

namespace {

namespace fx = ::kota::meta::fixtures;

TEST_SUITE(virtual_schema_recursive_first_access) {

// Each test deliberately does NOT call type_info_of<T>() beforehand.
// This exercises the exact code path that previously failed on Clang.

TEST_CASE(tree_node_fields_without_prior_type_info) {
    using S = virtual_schema<fx::TreeNode>;
    STATIC_EXPECT_EQ(S::count, 2U);
    STATIC_EXPECT_EQ(S::fields.size(), 2U);
    STATIC_EXPECT_EQ(S::fields[0].name, "value");
    STATIC_EXPECT_EQ(S::fields[1].name, "children");
}

TEST_CASE(shared_node_fields_without_prior_type_info) {
    using S = virtual_schema<fx::SharedNode>;
    STATIC_EXPECT_EQ(S::count, 3U);
    STATIC_EXPECT_EQ(S::fields[0].name, "label");
    STATIC_EXPECT_EQ(S::fields[1].name, "parent");
    STATIC_EXPECT_EQ(S::fields[2].name, "children");
}

TEST_CASE(linked_node_fields_without_prior_type_info) {
    using S = virtual_schema<fx::LinkedNode>;
    STATIC_EXPECT_EQ(S::count, 2U);
    STATIC_EXPECT_EQ(S::fields[0].name, "data");
    STATIC_EXPECT_EQ(S::fields[1].name, "next");
}

TEST_CASE(optional_recursive_fields_without_prior_type_info) {
    using S = virtual_schema<fx::OptionalRecursive>;
    STATIC_EXPECT_EQ(S::count, 2U);
    STATIC_EXPECT_EQ(S::fields[0].name, "id");
    STATIC_EXPECT_EQ(S::fields[1].name, "sub_items");
}

TEST_CASE(map_recursive_fields_without_prior_type_info) {
    using S = virtual_schema<fx::MapRecursive>;
    STATIC_EXPECT_EQ(S::count, 2U);
    STATIC_EXPECT_EQ(S::fields[0].name, "name");
    STATIC_EXPECT_EQ(S::fields[1].name, "nested");
}

TEST_CASE(mixed_recursive_fields_without_prior_type_info) {
    using S = virtual_schema<fx::MixedRecursive>;
    STATIC_EXPECT_EQ(S::count, 3U);
    STATIC_EXPECT_EQ(S::fields[0].name, "tag");
    STATIC_EXPECT_EQ(S::fields[1].name, "deep");
    STATIC_EXPECT_EQ(S::fields[2].name, "grouped");
}

TEST_CASE(variant_branch_mutual_recursion_without_prior_type_info) {
    using S = virtual_schema<fx::VariantBranch>;
    STATIC_EXPECT_EQ(S::count, 1U);
    STATIC_EXPECT_EQ(S::fields[0].name, "nodes");
}

};  // TEST_SUITE(virtual_schema_recursive_first_access)

}  // namespace

}  // namespace kota::meta
