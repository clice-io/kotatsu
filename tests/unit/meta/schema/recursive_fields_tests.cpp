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

ZEST_SUITE(virtual_schema_recursive_first_access){

    // Each test deliberately does NOT call type_info_of<T>() beforehand.
    // This exercises the exact code path that previously failed on Clang.

    ZEST_CASE(tree_node_fields_without_prior_type_info){using S = virtual_schema<fx::TreeNode>;
STATIC_EXPECT(S::count == 2U);
STATIC_EXPECT(S::fields.size() == 2U);
STATIC_EXPECT(S::fields[0].name == "value");
STATIC_EXPECT(S::fields[1].name == "children");

}  // namespace

ZEST_CASE(shared_node_fields_without_prior_type_info) {
    using S = virtual_schema<fx::SharedNode>;
    STATIC_EXPECT(S::count == 3U);
    STATIC_EXPECT(S::fields[0].name == "label");
    STATIC_EXPECT(S::fields[1].name == "parent");
    STATIC_EXPECT(S::fields[2].name == "children");
}

ZEST_CASE(linked_node_fields_without_prior_type_info) {
    using S = virtual_schema<fx::LinkedNode>;
    STATIC_EXPECT(S::count == 2U);
    STATIC_EXPECT(S::fields[0].name == "data");
    STATIC_EXPECT(S::fields[1].name == "next");
}

ZEST_CASE(optional_recursive_fields_without_prior_type_info) {
    using S = virtual_schema<fx::OptionalRecursive>;
    STATIC_EXPECT(S::count == 2U);
    STATIC_EXPECT(S::fields[0].name == "id");
    STATIC_EXPECT(S::fields[1].name == "sub_items");
}

ZEST_CASE(map_recursive_fields_without_prior_type_info) {
    using S = virtual_schema<fx::MapRecursive>;
    STATIC_EXPECT(S::count == 2U);
    STATIC_EXPECT(S::fields[0].name == "name");
    STATIC_EXPECT(S::fields[1].name == "nested");
}

ZEST_CASE(mixed_recursive_fields_without_prior_type_info) {
    using S = virtual_schema<fx::MixedRecursive>;
    STATIC_EXPECT(S::count == 3U);
    STATIC_EXPECT(S::fields[0].name == "tag");
    STATIC_EXPECT(S::fields[1].name == "deep");
    STATIC_EXPECT(S::fields[2].name == "grouped");
}

ZEST_CASE(variant_branch_mutual_recursion_without_prior_type_info) {
    using S = virtual_schema<fx::VariantBranch>;
    STATIC_EXPECT(S::count == 1U);
    STATIC_EXPECT(S::fields[0].name == "nodes");
}

};  // namespace kota::meta

}  // namespace

}  // namespace kota::meta
