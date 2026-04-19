#include "fixtures/schema/containers.h"
#include "fixtures/schema/primitives.h"
#include "fixtures/schema/schema_attrs.h"
#include "fixtures/schema/tagged.h"
#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"

namespace kota::meta {

namespace {

namespace fx = ::kota::meta::fixtures;

TEST_SUITE(virtual_schema_schema_attrs) {

TEST_CASE(simple_struct_fields) {
    STATIC_EXPECT_EQ(virtual_schema<fx::SimpleStruct>::count, 3U);

    constexpr auto& fields = virtual_schema<fx::SimpleStruct>::fields;

    STATIC_EXPECT_EQ(fields[0].name, "x");
    STATIC_EXPECT_EQ(fields[1].name, "name");
    STATIC_EXPECT_EQ(fields[2].name, "score");

    STATIC_EXPECT_EQ(fields[0].type().kind, type_kind::int32);
    STATIC_EXPECT_EQ(fields[1].type().kind, type_kind::string);
    STATIC_EXPECT_EQ(fields[2].type().kind, type_kind::float32);

    STATIC_EXPECT_EQ(fields[0].physical_index, 0U);
    STATIC_EXPECT_EQ(fields[1].physical_index, 1U);
    STATIC_EXPECT_EQ(fields[2].physical_index, 2U);

    // Offsets: first at 0, strictly increasing
    STATIC_EXPECT_EQ(fields[0].offset, 0U);
    STATIC_EXPECT_TRUE(fields[1].offset > fields[0].offset);
    STATIC_EXPECT_TRUE(fields[2].offset > fields[1].offset);

    // All flags false for plain struct
    STATIC_EXPECT_FALSE(fields[0].has_default);
    STATIC_EXPECT_FALSE(fields[0].is_literal);
    STATIC_EXPECT_FALSE(fields[0].has_skip_if);
    STATIC_EXPECT_FALSE(fields[0].has_behavior);
    STATIC_EXPECT_EQ(fields[0].aliases.size(), 0U);
    STATIC_EXPECT_FALSE(fields[1].has_default);
    STATIC_EXPECT_FALSE(fields[1].is_literal);
    STATIC_EXPECT_FALSE(fields[1].has_skip_if);
    STATIC_EXPECT_FALSE(fields[1].has_behavior);
    STATIC_EXPECT_EQ(fields[1].aliases.size(), 0U);
    STATIC_EXPECT_FALSE(fields[2].has_default);
    STATIC_EXPECT_FALSE(fields[2].is_literal);
    STATIC_EXPECT_FALSE(fields[2].has_skip_if);
    STATIC_EXPECT_FALSE(fields[2].has_behavior);
    STATIC_EXPECT_EQ(fields[2].aliases.size(), 0U);
}

TEST_CASE(rename_and_skip) {
    // AnnotatedStruct: user_id(rename<"id">), internal(skip), value
    STATIC_EXPECT_EQ(virtual_schema<fx::AnnotatedStruct>::count, 2U);

    constexpr auto& fields = virtual_schema<fx::AnnotatedStruct>::fields;

    STATIC_EXPECT_EQ(fields[0].name, "id");
    STATIC_EXPECT_EQ(fields[0].type().kind, type_kind::int32);
    STATIC_EXPECT_EQ(fields[0].physical_index, 0U);

    STATIC_EXPECT_EQ(fields[1].name, "value");
    STATIC_EXPECT_EQ(fields[1].type().kind, type_kind::float32);
    STATIC_EXPECT_EQ(fields[1].physical_index, 2U);
}

TEST_CASE(alias) {
    constexpr auto& fields = virtual_schema<fx::AliasStruct>::fields;

    STATIC_EXPECT_EQ(fields[0].aliases.size(), 2U);
    STATIC_EXPECT_EQ(fields[0].aliases[0], "user_id");
    STATIC_EXPECT_EQ(fields[0].aliases[1], "userId");

    // Plain field has no aliases
    STATIC_EXPECT_EQ(fields[1].aliases.size(), 0U);
}

TEST_CASE(flatten) {
    // Outer: x(1) + Inner{a,b}(2) + y(1) = 4 fields
    STATIC_EXPECT_EQ(virtual_schema<fx::Outer>::count, 4U);

    constexpr auto& fields = virtual_schema<fx::Outer>::fields;

    STATIC_EXPECT_EQ(fields[0].name, "x");
    STATIC_EXPECT_EQ(fields[1].name, "a");
    STATIC_EXPECT_EQ(fields[2].name, "b");
    STATIC_EXPECT_EQ(fields[3].name, "y");

    STATIC_EXPECT_EQ(fields[0].type().kind, type_kind::int32);
    STATIC_EXPECT_EQ(fields[1].type().kind, type_kind::int32);
    STATIC_EXPECT_EQ(fields[2].type().kind, type_kind::int32);
    STATIC_EXPECT_EQ(fields[3].type().kind, type_kind::int32);

    // Offsets: strictly increasing, flattened offsets match outer+inner layout
    STATIC_EXPECT_EQ(fields[0].offset, 0U);
    STATIC_EXPECT_TRUE(fields[1].offset > fields[0].offset);
    STATIC_EXPECT_TRUE(fields[2].offset > fields[1].offset);
    STATIC_EXPECT_TRUE(fields[3].offset > fields[2].offset);

    constexpr auto outer_inner_offset = field_offset<fx::Outer>(1);
    constexpr auto inner_a_offset = field_offset<fx::Inner>(0);
    constexpr auto inner_b_offset = field_offset<fx::Inner>(1);
    STATIC_EXPECT_EQ(fields[1].offset, outer_inner_offset + inner_a_offset);
    STATIC_EXPECT_EQ(fields[2].offset, outer_inner_offset + inner_b_offset);
}

TEST_CASE(deep_flatten) {
    // DeepOuter: head + Middle{m, DeepInner{p,q}} + tail = 5 fields
    STATIC_EXPECT_EQ(virtual_schema<fx::DeepOuter>::count, 5U);

    constexpr auto& fields = virtual_schema<fx::DeepOuter>::fields;

    STATIC_EXPECT_EQ(fields[0].name, "head");
    STATIC_EXPECT_EQ(fields[1].name, "m");
    STATIC_EXPECT_EQ(fields[2].name, "p");
    STATIC_EXPECT_EQ(fields[3].name, "q");
    STATIC_EXPECT_EQ(fields[4].name, "tail");

    STATIC_EXPECT_EQ(fields[0].type().kind, type_kind::int32);
    STATIC_EXPECT_EQ(fields[1].type().kind, type_kind::int32);
    STATIC_EXPECT_EQ(fields[2].type().kind, type_kind::int32);
    STATIC_EXPECT_EQ(fields[3].type().kind, type_kind::int32);
    STATIC_EXPECT_EQ(fields[4].type().kind, type_kind::int32);

    // Offsets must be strictly increasing
    STATIC_EXPECT_TRUE(fields[1].offset > fields[0].offset);
    STATIC_EXPECT_TRUE(fields[2].offset > fields[1].offset);
    STATIC_EXPECT_TRUE(fields[3].offset > fields[2].offset);
    STATIC_EXPECT_TRUE(fields[4].offset > fields[3].offset);
}

TEST_CASE(default_value_and_literal) {
    STATIC_EXPECT_EQ(virtual_schema<fx::DefaultLiteralStruct>::count, 3U);

    constexpr auto& fields = virtual_schema<fx::DefaultLiteralStruct>::fields;

    STATIC_EXPECT_TRUE(fields[0].has_default);
    STATIC_EXPECT_FALSE(fields[0].is_literal);

    STATIC_EXPECT_TRUE(fields[1].is_literal);
    STATIC_EXPECT_FALSE(fields[1].has_default);

    STATIC_EXPECT_FALSE(fields[2].has_default);
    STATIC_EXPECT_FALSE(fields[2].is_literal);
}

TEST_CASE(deny_unknown_default_false) {
    // deny_unknown_fields is resolved at the serde dispatch level, not on the
    // struct type itself. For regular reflectable structs, deny_unknown is always false.
    STATIC_EXPECT_FALSE(virtual_schema<fx::SimpleStruct>::deny_unknown);
    STATIC_EXPECT_FALSE(virtual_schema<fx::AnnotatedStruct>::deny_unknown);
}

TEST_CASE(nested_field_type_info) {
    // NestedStruct: items is vector<SimpleStruct>
    STATIC_EXPECT_EQ(virtual_schema<fx::NestedStruct>::count, 1U);

    constexpr auto& fields = virtual_schema<fx::NestedStruct>::fields;
    STATIC_EXPECT_EQ(fields[0].name, "items");
    STATIC_EXPECT_EQ(fields[0].type().kind, type_kind::array);

    constexpr auto arr = static_cast<const array_type_info&>(fields[0].type());
    constexpr auto elem = arr.element();
    STATIC_EXPECT_EQ(elem.kind, type_kind::structure);
}

TEST_CASE(tagged_field_type_info) {
    constexpr auto& fields = virtual_schema<fx::TaggedFieldStruct>::fields;
    STATIC_EXPECT_EQ(fields.size(), 3U);

    constexpr auto ext = static_cast<const variant_type_info&>(fields[0].type());
    STATIC_EXPECT_EQ(ext.tagging, tag_mode::external);
    STATIC_EXPECT_EQ(ext.tag_field, "");
    STATIC_EXPECT_EQ(ext.content_field, "");
    STATIC_EXPECT_EQ(ext.alt_names.size(), 2U);
    STATIC_EXPECT_EQ(ext.alt_names[0], "integer");
    STATIC_EXPECT_EQ(ext.alt_names[1], "text");

    constexpr auto in = static_cast<const variant_type_info&>(fields[1].type());
    STATIC_EXPECT_EQ(in.tagging, tag_mode::internal);
    STATIC_EXPECT_EQ(in.tag_field, "kind");
    STATIC_EXPECT_EQ(in.content_field, "");
    STATIC_EXPECT_EQ(in.alt_names.size(), 2U);
    STATIC_EXPECT_EQ(in.alt_names[0], "circle");
    STATIC_EXPECT_EQ(in.alt_names[1], "rect");

    constexpr auto adj = static_cast<const variant_type_info&>(fields[2].type());
    STATIC_EXPECT_EQ(adj.tagging, tag_mode::adjacent);
    STATIC_EXPECT_EQ(adj.tag_field, "type");
    STATIC_EXPECT_EQ(adj.content_field, "value");
    STATIC_EXPECT_EQ(adj.alt_names.size(), 2U);
    STATIC_EXPECT_EQ(adj.alt_names[0], "integer");
    STATIC_EXPECT_EQ(adj.alt_names[1], "text");
}

};  // TEST_SUITE(virtual_schema_schema_attrs)

}  // namespace

}  // namespace kota::meta
