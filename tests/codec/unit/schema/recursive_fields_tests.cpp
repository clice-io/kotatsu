// virtual_schema<T>::fields of a recursive type, as the first use of the type's info in the TU.
//
// build_fields<T>() stores type_info::of<child> function pointers in field_info. For a
// recursive type they instantiate a chain back to the struct's info, info_node<T>::value,
// whose initializer references fields.data(). Evaluated on its own, not through `value`,
// `fields` makes Clang see a circular dependency; virtual_schema::fields reads it through
// `value`, as a sub-expression of which it resolves without re-entry.

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "fixtures/recursive.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/codec/type_info.h"

namespace kotatsu::codec {

namespace {

namespace fx = ::kotatsu::test;

ZEST_SUITE(codec_schema_recursive_fields) {

// No case calls type_info::of<T>() first: each enters the info through `fields`.

ZEST_CASE(tree_node_fields_without_prior_type_info) {
    using S = virtual_schema<fx::TreeNode>;
    ZSTATIC_EXPECT(S::fields.size() == 2U);
    ZSTATIC_EXPECT(S::fields.size() == 2U);
    ZSTATIC_EXPECT(S::fields[0].name == "value");
    ZSTATIC_EXPECT(S::fields[1].name == "children");
}

ZEST_CASE(shared_node_fields_without_prior_type_info) {
    using S = virtual_schema<fx::SharedNode>;
    ZSTATIC_EXPECT(S::fields.size() == 3U);
    ZSTATIC_EXPECT(S::fields[0].name == "label");
    ZSTATIC_EXPECT(S::fields[1].name == "parent");
    ZSTATIC_EXPECT(S::fields[2].name == "children");
}

ZEST_CASE(linked_node_fields_without_prior_type_info) {
    using S = virtual_schema<fx::LinkedNode>;
    ZSTATIC_EXPECT(S::fields.size() == 2U);
    ZSTATIC_EXPECT(S::fields[0].name == "data");
    ZSTATIC_EXPECT(S::fields[1].name == "next");
}

ZEST_CASE(optional_recursive_fields_without_prior_type_info) {
    using S = virtual_schema<fx::OptionalRecursive>;
    ZSTATIC_EXPECT(S::fields.size() == 2U);
    ZSTATIC_EXPECT(S::fields[0].name == "id");
    ZSTATIC_EXPECT(S::fields[1].name == "sub_items");
}

ZEST_CASE(map_recursive_fields_without_prior_type_info) {
    using S = virtual_schema<fx::MapRecursive>;
    ZSTATIC_EXPECT(S::fields.size() == 2U);
    ZSTATIC_EXPECT(S::fields[0].name == "name");
    ZSTATIC_EXPECT(S::fields[1].name == "nested");
}

ZEST_CASE(mixed_recursive_fields_without_prior_type_info) {
    using S = virtual_schema<fx::MixedRecursive>;
    ZSTATIC_EXPECT(S::fields.size() == 3U);
    ZSTATIC_EXPECT(S::fields[0].name == "tag");
    ZSTATIC_EXPECT(S::fields[1].name == "deep");
    ZSTATIC_EXPECT(S::fields[2].name == "grouped");
}

ZEST_CASE(variant_branch_mutual_recursion_without_prior_type_info) {
    using S = virtual_schema<fx::VariantBranch>;
    ZSTATIC_EXPECT(S::fields.size() == 1U);
    ZSTATIC_EXPECT(S::fields[0].name == "nodes");
}

};  // ZEST_SUITE(codec_schema_recursive_fields)

}  // namespace

}  // namespace kotatsu::codec
