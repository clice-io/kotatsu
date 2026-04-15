#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "eventide/zest/zest.h"
#include "eventide/reflection/schema.h"

namespace eventide::refl {

namespace test_schema {

enum class color { red, green, blue };

struct SimpleStruct {
    int x;
    std::string name;
    float score;
};

struct RenameTarget {
    int user_name;
    std::string display_name;
};

using RenamedRoot = annotation<RenameTarget, attrs::rename_all<rename_policy::lower_camel>>;

using StrictRoot = annotation<RenameTarget, attrs::deny_unknown_fields>;

struct Circle {
    double radius;
};

struct Rect {
    double width;
    double height;
};

using TaggedRoot = annotation<std::variant<Circle, Rect>,
                              attrs::internally_tagged<"kind">::names<"circle", "rect">>;

enum class HugeUnsignedEnum : std::uint64_t {
    zero = 0,
    max = std::numeric_limits<std::uint64_t>::max(),
};

struct TreeNode {
    std::string value;
    std::vector<TreeNode> children;
};

struct LinkedNode {
    int data;
    std::unique_ptr<LinkedNode> next;
};

struct SharedNode {
    std::string label;
    std::shared_ptr<SharedNode> parent;
    std::vector<std::shared_ptr<SharedNode>> children;
};

struct OptionalRecursive {
    int id;
    std::optional<std::vector<OptionalRecursive>> sub_items;
};

struct MapRecursive {
    std::string name;
    std::map<std::string, MapRecursive> nested;
};

struct VariantLeaf {
    int val;
};

struct VariantBranch {
    std::vector<std::variant<VariantLeaf, VariantBranch>> nodes;
};

struct MixedRecursive {
    std::string tag;
    std::optional<std::unique_ptr<MixedRecursive>> deep;
    std::map<std::string, std::vector<MixedRecursive>> grouped;
};

struct disabled_range;

struct disabled_range_iter {
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = disabled_range;
    using difference_type = std::ptrdiff_t;

    auto operator*() const -> disabled_range;

    auto operator++() -> disabled_range_iter& {
        return *this;
    }

    auto operator++(int) -> disabled_range_iter {
        return *this;
    }

    auto operator==(std::default_sentinel_t) const -> bool {
        return true;
    }
};

struct disabled_range {
    auto begin() const -> disabled_range_iter {
        return {};
    }

    auto end() const -> std::default_sentinel_t {
        return {};
    }
};

inline auto disabled_range_iter::operator*() const -> disabled_range {
    return {};
}

static_assert(std::ranges::input_range<disabled_range>);
static_assert(eventide::format_kind<disabled_range> == eventide::range_format::disabled);

}  // namespace test_schema

namespace {

TEST_SUITE(virtual_schema_type_info) {

TEST_CASE(scalar_helpers) {
    // int32: signed integer, numeric, scalar
    constexpr auto& int_info = *type_info_of<int, default_config>();
    EXPECT_EQ(int_info.kind, type_kind::int32);
    EXPECT_TRUE(int_info.is_integer());
    EXPECT_TRUE(int_info.is_signed_integer());
    EXPECT_FALSE(int_info.is_unsigned_integer());
    EXPECT_FALSE(int_info.is_floating());
    EXPECT_TRUE(int_info.is_numeric());
    EXPECT_TRUE(int_info.is_scalar());

    // uint64: unsigned integer, numeric, scalar
    constexpr auto& u64_info = *type_info_of<std::uint64_t, default_config>();
    EXPECT_TRUE(u64_info.is_integer());
    EXPECT_FALSE(u64_info.is_signed_integer());
    EXPECT_TRUE(u64_info.is_unsigned_integer());
    EXPECT_TRUE(u64_info.is_numeric());
    EXPECT_TRUE(u64_info.is_scalar());

    // double: floating, numeric, scalar
    constexpr auto& dbl_info = *type_info_of<double, default_config>();
    EXPECT_FALSE(dbl_info.is_integer());
    EXPECT_TRUE(dbl_info.is_floating());
    EXPECT_TRUE(dbl_info.is_numeric());
    EXPECT_TRUE(dbl_info.is_scalar());

    // bool: scalar but not numeric
    constexpr auto& bool_info = *type_info_of<bool, default_config>();
    EXPECT_TRUE(bool_info.is_scalar());
    EXPECT_FALSE(bool_info.is_numeric());
    EXPECT_FALSE(bool_info.is_integer());
    EXPECT_FALSE(bool_info.is_floating());

    // string: scalar but not numeric
    constexpr auto& str_info = *type_info_of<std::string, default_config>();
    EXPECT_EQ(str_info.kind, type_kind::string);
    EXPECT_TRUE(str_info.is_scalar());
    EXPECT_FALSE(str_info.is_numeric());

    // enum: scalar but not numeric
    constexpr auto& enum_info = *type_info_of<test_schema::color, default_config>();
    EXPECT_EQ(enum_info.kind, type_kind::enumeration);
    EXPECT_TRUE(enum_info.is_scalar());
    EXPECT_FALSE(enum_info.is_numeric());
}

TEST_CASE(compound_types) {
    // vector<int> -> array with int32 element
    {
        constexpr auto& info = *type_info_of<std::vector<int>, default_config>();
        EXPECT_EQ(info.kind, type_kind::array);
        EXPECT_FALSE(info.is_scalar());
        auto* arr = static_cast<const array_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(arr->element->kind, type_kind::int32);
    }

    // set<int> -> set with int32 element
    {
        constexpr auto& info = *type_info_of<std::set<int>, default_config>();
        EXPECT_EQ(info.kind, type_kind::set);
        EXPECT_FALSE(info.is_scalar());
        auto* arr = static_cast<const array_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(arr->element->kind, type_kind::int32);
    }

    // map<string, int> -> map with string key, int32 value
    {
        constexpr auto& info = *type_info_of<std::map<std::string, int>, default_config>();
        EXPECT_EQ(info.kind, type_kind::map);
        EXPECT_FALSE(info.is_scalar());
        auto* m = static_cast<const map_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(m->key->kind, type_kind::string);
        EXPECT_EQ(m->value->kind, type_kind::int32);
    }

    // optional<int> -> optional with int32 inner
    {
        constexpr auto& info = *type_info_of<std::optional<int>, default_config>();
        EXPECT_EQ(info.kind, type_kind::optional);
        auto* opt = static_cast<const optional_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(opt->inner->kind, type_kind::int32);
    }

    // unique_ptr<int> -> pointer with int32 inner
    {
        constexpr auto& info = *type_info_of<std::unique_ptr<int>, default_config>();
        EXPECT_EQ(info.kind, type_kind::pointer);
        auto* ptr = static_cast<const optional_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(ptr->inner->kind, type_kind::int32);
    }

    // shared_ptr<string> -> pointer with string inner
    {
        constexpr auto& info = *type_info_of<std::shared_ptr<std::string>, default_config>();
        EXPECT_EQ(info.kind, type_kind::pointer);
        auto* ptr = static_cast<const optional_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(ptr->inner->kind, type_kind::string);
    }

    // variant<int, string> -> variant with 2 alternatives
    {
        constexpr auto& info = *type_info_of<std::variant<int, std::string>, default_config>();
        EXPECT_EQ(info.kind, type_kind::variant);
        auto* var = static_cast<const variant_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(var->alternatives.size(), 2U);
        EXPECT_EQ(var->alternatives[0]->kind, type_kind::int32);
        EXPECT_EQ(var->alternatives[1]->kind, type_kind::string);
    }

    // pair<int, float> -> tuple with 2 elements
    {
        constexpr auto& info = *type_info_of<std::pair<int, float>, default_config>();
        EXPECT_EQ(info.kind, type_kind::tuple);
        auto* tup = static_cast<const tuple_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(tup->elements.size(), 2U);
        EXPECT_EQ(tup->elements[0]->kind, type_kind::int32);
        EXPECT_EQ(tup->elements[1]->kind, type_kind::float32);
    }

    // tuple<int, double, string> -> tuple with 3 elements
    {
        constexpr auto& info =
            *type_info_of<std::tuple<int, double, std::string>, default_config>();
        EXPECT_EQ(info.kind, type_kind::tuple);
        auto* tup = static_cast<const tuple_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(tup->elements.size(), 3U);
        EXPECT_EQ(tup->elements[0]->kind, type_kind::int32);
        EXPECT_EQ(tup->elements[1]->kind, type_kind::float64);
        EXPECT_EQ(tup->elements[2]->kind, type_kind::string);
    }

    // SimpleStruct -> structure
    {
        constexpr auto& info = *type_info_of<test_schema::SimpleStruct, default_config>();
        EXPECT_EQ(info.kind, type_kind::structure);
        EXPECT_FALSE(info.is_scalar());
    }
}

TEST_CASE(nested_type_info) {
    // vector<optional<int>> -> array -> optional -> int32
    {
        constexpr auto& info = *type_info_of<std::vector<std::optional<int>>, default_config>();
        EXPECT_EQ(info.kind, type_kind::array);
        auto* arr = static_cast<const array_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(arr->element->kind, type_kind::optional);
        auto* opt = static_cast<const optional_type_info*>(arr->element);
        EXPECT_EQ(opt->inner->kind, type_kind::int32);
    }

    // map<string, vector<int>> -> map -> string key, array value -> int32 element
    {
        constexpr auto& info =
            *type_info_of<std::map<std::string, std::vector<int>>, default_config>();
        EXPECT_EQ(info.kind, type_kind::map);
        auto* m = static_cast<const map_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(m->key->kind, type_kind::string);
        EXPECT_EQ(m->value->kind, type_kind::array);
        auto* inner_arr = static_cast<const array_type_info*>(m->value);
        EXPECT_EQ(inner_arr->element->kind, type_kind::int32);
    }

    // set<vector<string>> -> set -> array -> string
    {
        constexpr auto& info = *type_info_of<std::set<std::vector<std::string>>, default_config>();
        EXPECT_EQ(info.kind, type_kind::set);
        auto* s = static_cast<const array_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(s->element->kind, type_kind::array);
        auto* inner_arr = static_cast<const array_type_info*>(s->element);
        EXPECT_EQ(inner_arr->element->kind, type_kind::string);
    }

    // unique_ptr<vector<int>> -> pointer -> array -> int32
    {
        constexpr auto& info = *type_info_of<std::unique_ptr<std::vector<int>>, default_config>();
        EXPECT_EQ(info.kind, type_kind::pointer);
        auto* ptr = static_cast<const optional_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(ptr->inner->kind, type_kind::array);
        auto* arr = static_cast<const array_type_info*>(ptr->inner);
        EXPECT_EQ(arr->element->kind, type_kind::int32);
    }
}

TEST_CASE(annotated_type_info) {
    {
        constexpr auto& info = *type_info_of<test_schema::RenamedRoot, default_config>();
        EXPECT_EQ(info.kind, type_kind::structure);

        auto* si = static_cast<const struct_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(si->fields.size(), 2U);
        EXPECT_EQ(si->fields[0].name, "userName");
        EXPECT_EQ(si->fields[1].name, "displayName");
        EXPECT_FALSE(si->deny_unknown);
    }

    {
        constexpr auto& info = *type_info_of<test_schema::StrictRoot, default_config>();
        EXPECT_EQ(info.kind, type_kind::structure);

        auto* si = static_cast<const struct_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(si->fields.size(), 2U);
        EXPECT_TRUE(si->deny_unknown);
    }

    {
        constexpr auto& info = *type_info_of<test_schema::TaggedRoot, default_config>();
        EXPECT_EQ(info.kind, type_kind::variant);

        auto* var = static_cast<const variant_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(var->tagging, tag_mode::internal);
        EXPECT_EQ(var->alternatives.size(), 2U);
        EXPECT_EQ(var->alternatives[0]->kind, type_kind::structure);
        EXPECT_EQ(var->alternatives[1]->kind, type_kind::structure);
        EXPECT_EQ(var->tag_field, "kind");
        EXPECT_EQ(var->content_field, "");
        EXPECT_EQ(var->alt_names.size(), 2U);
        EXPECT_EQ(var->alt_names[0], "circle");
        EXPECT_EQ(var->alt_names[1], "rect");
    }

    {
        constexpr auto& info = *type_info_of<test_schema::HugeUnsignedEnum, default_config>();
        EXPECT_EQ(info.kind, type_kind::enumeration);

        auto* ei = static_cast<const enum_type_info*>(static_cast<const type_info*>(&info));
        EXPECT_EQ(ei->underlying_kind, type_kind::uint64);
        EXPECT_EQ(ei->member_values.size(), 0U);
        EXPECT_EQ(ei->member_u64_values.size(), 2U);
        EXPECT_TRUE((ei->member_u64_values[0] == 0U &&
                     ei->member_u64_values[1] == std::numeric_limits<std::uint64_t>::max()) ||
                    (ei->member_u64_values[1] == 0U &&
                     ei->member_u64_values[0] == std::numeric_limits<std::uint64_t>::max()));
    }
}

TEST_CASE(recursive_tree_node) {
    constexpr auto& info = *type_info_of<test_schema::TreeNode, default_config>();
    EXPECT_EQ(info.kind, type_kind::structure);

    auto* si = static_cast<const struct_type_info*>(static_cast<const type_info*>(&info));
    EXPECT_EQ(si->fields.size(), 2U);
    EXPECT_EQ(si->fields[0].name, "value");
    EXPECT_EQ(si->fields[0].type->kind, type_kind::string);

    EXPECT_EQ(si->fields[1].name, "children");
    EXPECT_EQ(si->fields[1].type->kind, type_kind::array);
    auto* arr = static_cast<const array_type_info*>(si->fields[1].type);
    EXPECT_EQ(arr->element->kind, type_kind::structure);
    EXPECT_EQ(arr->element->type_name, info.type_name);
}

TEST_CASE(recursive_linked_list_unique_ptr) {
    constexpr auto& info = *type_info_of<test_schema::LinkedNode, default_config>();
    EXPECT_EQ(info.kind, type_kind::structure);

    auto* si = static_cast<const struct_type_info*>(static_cast<const type_info*>(&info));
    EXPECT_EQ(si->fields.size(), 2U);
    EXPECT_EQ(si->fields[0].name, "data");
    EXPECT_EQ(si->fields[0].type->kind, type_kind::int32);

    EXPECT_EQ(si->fields[1].name, "next");
    EXPECT_EQ(si->fields[1].type->kind, type_kind::pointer);
    auto* ptr = static_cast<const optional_type_info*>(si->fields[1].type);
    EXPECT_EQ(ptr->inner->kind, type_kind::structure);
    EXPECT_EQ(ptr->inner->type_name, info.type_name);
}

TEST_CASE(recursive_shared_ptr_with_vector) {
    constexpr auto& info = *type_info_of<test_schema::SharedNode, default_config>();
    EXPECT_EQ(info.kind, type_kind::structure);

    auto* si = static_cast<const struct_type_info*>(static_cast<const type_info*>(&info));
    EXPECT_EQ(si->fields.size(), 3U);

    EXPECT_EQ(si->fields[0].name, "label");
    EXPECT_EQ(si->fields[0].type->kind, type_kind::string);

    EXPECT_EQ(si->fields[1].name, "parent");
    EXPECT_EQ(si->fields[1].type->kind, type_kind::pointer);
    auto* parent_ptr = static_cast<const optional_type_info*>(si->fields[1].type);
    EXPECT_EQ(parent_ptr->inner->kind, type_kind::structure);
    EXPECT_EQ(parent_ptr->inner->type_name, info.type_name);

    EXPECT_EQ(si->fields[2].name, "children");
    EXPECT_EQ(si->fields[2].type->kind, type_kind::array);
    auto* arr = static_cast<const array_type_info*>(si->fields[2].type);
    EXPECT_EQ(arr->element->kind, type_kind::pointer);
    auto* child_ptr = static_cast<const optional_type_info*>(arr->element);
    EXPECT_EQ(child_ptr->inner->kind, type_kind::structure);
    EXPECT_EQ(child_ptr->inner->type_name, info.type_name);
}

TEST_CASE(recursive_optional_vector) {
    constexpr auto& info = *type_info_of<test_schema::OptionalRecursive, default_config>();
    EXPECT_EQ(info.kind, type_kind::structure);

    auto* si = static_cast<const struct_type_info*>(static_cast<const type_info*>(&info));
    EXPECT_EQ(si->fields.size(), 2U);
    EXPECT_EQ(si->fields[0].name, "id");
    EXPECT_EQ(si->fields[0].type->kind, type_kind::int32);

    EXPECT_EQ(si->fields[1].name, "sub_items");
    EXPECT_EQ(si->fields[1].type->kind, type_kind::optional);
    auto* opt = static_cast<const optional_type_info*>(si->fields[1].type);
    EXPECT_EQ(opt->inner->kind, type_kind::array);
    auto* arr = static_cast<const array_type_info*>(opt->inner);
    EXPECT_EQ(arr->element->kind, type_kind::structure);
    EXPECT_EQ(arr->element->type_name, info.type_name);
}

TEST_CASE(recursive_map_values) {
    constexpr auto& info = *type_info_of<test_schema::MapRecursive, default_config>();
    EXPECT_EQ(info.kind, type_kind::structure);

    auto* si = static_cast<const struct_type_info*>(static_cast<const type_info*>(&info));
    EXPECT_EQ(si->fields.size(), 2U);
    EXPECT_EQ(si->fields[0].name, "name");
    EXPECT_EQ(si->fields[0].type->kind, type_kind::string);

    EXPECT_EQ(si->fields[1].name, "nested");
    EXPECT_EQ(si->fields[1].type->kind, type_kind::map);
    auto* m = static_cast<const map_type_info*>(si->fields[1].type);
    EXPECT_EQ(m->key->kind, type_kind::string);
    EXPECT_EQ(m->value->kind, type_kind::structure);
    EXPECT_EQ(m->value->type_name, info.type_name);
}

TEST_CASE(recursive_variant_tree) {
    constexpr auto& branch_info = *type_info_of<test_schema::VariantBranch, default_config>();
    EXPECT_EQ(branch_info.kind, type_kind::structure);

    auto* si = static_cast<const struct_type_info*>(static_cast<const type_info*>(&branch_info));
    EXPECT_EQ(si->fields.size(), 1U);
    EXPECT_EQ(si->fields[0].name, "nodes");
    EXPECT_EQ(si->fields[0].type->kind, type_kind::array);

    auto* arr = static_cast<const array_type_info*>(si->fields[0].type);
    EXPECT_EQ(arr->element->kind, type_kind::variant);
    auto* var = static_cast<const variant_type_info*>(arr->element);
    EXPECT_EQ(var->alternatives.size(), 2U);

    constexpr auto& leaf_info = *type_info_of<test_schema::VariantLeaf, default_config>();
    EXPECT_EQ(var->alternatives[0]->kind, type_kind::structure);
    EXPECT_EQ(var->alternatives[0]->type_name, leaf_info.type_name);
    EXPECT_EQ(var->alternatives[1]->kind, type_kind::structure);
    EXPECT_EQ(var->alternatives[1]->type_name, branch_info.type_name);
}

TEST_CASE(recursive_mixed_deep) {
    constexpr auto& info = *type_info_of<test_schema::MixedRecursive, default_config>();
    EXPECT_EQ(info.kind, type_kind::structure);

    auto* si = static_cast<const struct_type_info*>(static_cast<const type_info*>(&info));
    EXPECT_EQ(si->fields.size(), 3U);

    // tag: string
    EXPECT_EQ(si->fields[0].name, "tag");
    EXPECT_EQ(si->fields[0].type->kind, type_kind::string);

    // deep: optional -> pointer -> structure (self)
    EXPECT_EQ(si->fields[1].name, "deep");
    EXPECT_EQ(si->fields[1].type->kind, type_kind::optional);
    auto* opt = static_cast<const optional_type_info*>(si->fields[1].type);
    EXPECT_EQ(opt->inner->kind, type_kind::pointer);
    auto* ptr = static_cast<const optional_type_info*>(opt->inner);
    EXPECT_EQ(ptr->inner->kind, type_kind::structure);
    EXPECT_EQ(ptr->inner->type_name, info.type_name);

    // grouped: map<string, vector<self>>
    EXPECT_EQ(si->fields[2].name, "grouped");
    EXPECT_EQ(si->fields[2].type->kind, type_kind::map);
    auto* m = static_cast<const map_type_info*>(si->fields[2].type);
    EXPECT_EQ(m->key->kind, type_kind::string);
    EXPECT_EQ(m->value->kind, type_kind::array);
    auto* arr = static_cast<const array_type_info*>(m->value);
    EXPECT_EQ(arr->element->kind, type_kind::structure);
    EXPECT_EQ(arr->element->type_name, info.type_name);
}

TEST_CASE(disabled_range_type_info_is_unknown) {
    EXPECT_EQ(kind_of<test_schema::disabled_range>(), type_kind::unknown);

    constexpr auto& info = *type_info_of<test_schema::disabled_range, default_config>();
    EXPECT_EQ(info.kind, type_kind::unknown);
}

};  // TEST_SUITE(virtual_schema_type_info)

}  // namespace

}  // namespace eventide::refl
