#include <cstddef>

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
    EXPECT_EQ(virtual_schema<fx::SimpleStruct>::count, 3U);

    constexpr auto& fields = virtual_schema<fx::SimpleStruct>::fields;

    EXPECT_EQ(fields[0].name, "x");
    EXPECT_EQ(fields[1].name, "name");
    EXPECT_EQ(fields[2].name, "score");

    EXPECT_EQ(fields[0].type().kind, type_kind::int32);
    EXPECT_EQ(fields[1].type().kind, type_kind::string);
    EXPECT_EQ(fields[2].type().kind, type_kind::float32);

    EXPECT_EQ(fields[0].physical_index, 0U);
    EXPECT_EQ(fields[1].physical_index, 1U);
    EXPECT_EQ(fields[2].physical_index, 2U);

    // Offsets: first at 0, strictly increasing
    EXPECT_EQ(fields[0].offset, 0U);
    EXPECT_TRUE(fields[1].offset > fields[0].offset);
    EXPECT_TRUE(fields[2].offset > fields[1].offset);

    // All flags false for plain struct
    for(std::size_t i = 0; i < 3; ++i) {
        EXPECT_FALSE(fields[i].has_default);
        EXPECT_FALSE(fields[i].is_literal);
        EXPECT_FALSE(fields[i].has_skip_if);
        EXPECT_FALSE(fields[i].has_behavior);
        EXPECT_EQ(fields[i].aliases.size(), 0U);
    }
}

TEST_CASE(rename_and_skip) {
    // AnnotatedStruct: user_id(rename<"id">), internal(skip), value
    EXPECT_EQ(virtual_schema<fx::AnnotatedStruct>::count, 2U);

    constexpr auto& fields = virtual_schema<fx::AnnotatedStruct>::fields;

    EXPECT_EQ(fields[0].name, "id");
    EXPECT_EQ(fields[0].type().kind, type_kind::int32);
    EXPECT_EQ(fields[0].physical_index, 0U);

    EXPECT_EQ(fields[1].name, "value");
    EXPECT_EQ(fields[1].type().kind, type_kind::float32);
    EXPECT_EQ(fields[1].physical_index, 2U);
}

TEST_CASE(alias) {
    constexpr auto& fields = virtual_schema<fx::AliasStruct>::fields;

    EXPECT_EQ(fields[0].aliases.size(), 2U);
    EXPECT_EQ(fields[0].aliases[0], "user_id");
    EXPECT_EQ(fields[0].aliases[1], "userId");

    // Plain field has no aliases
    EXPECT_EQ(fields[1].aliases.size(), 0U);
}

TEST_CASE(flatten) {
    // Outer: x(1) + Inner{a,b}(2) + y(1) = 4 fields
    EXPECT_EQ(virtual_schema<fx::Outer>::count, 4U);

    constexpr auto& fields = virtual_schema<fx::Outer>::fields;

    EXPECT_EQ(fields[0].name, "x");
    EXPECT_EQ(fields[1].name, "a");
    EXPECT_EQ(fields[2].name, "b");
    EXPECT_EQ(fields[3].name, "y");

    for(std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(fields[i].type().kind, type_kind::int32);
    }

    // Offsets: strictly increasing, flattened offsets match outer+inner layout
    EXPECT_EQ(fields[0].offset, 0U);
    EXPECT_TRUE(fields[1].offset > fields[0].offset);
    EXPECT_TRUE(fields[2].offset > fields[1].offset);
    EXPECT_TRUE(fields[3].offset > fields[2].offset);

    constexpr auto outer_inner_offset = field_offset<fx::Outer>(1);
    constexpr auto inner_a_offset = field_offset<fx::Inner>(0);
    constexpr auto inner_b_offset = field_offset<fx::Inner>(1);
    EXPECT_EQ(fields[1].offset, outer_inner_offset + inner_a_offset);
    EXPECT_EQ(fields[2].offset, outer_inner_offset + inner_b_offset);
}

TEST_CASE(deep_flatten) {
    // DeepOuter: head + Middle{m, DeepInner{p,q}} + tail = 5 fields
    EXPECT_EQ(virtual_schema<fx::DeepOuter>::count, 5U);

    constexpr auto& fields = virtual_schema<fx::DeepOuter>::fields;

    EXPECT_EQ(fields[0].name, "head");
    EXPECT_EQ(fields[1].name, "m");
    EXPECT_EQ(fields[2].name, "p");
    EXPECT_EQ(fields[3].name, "q");
    EXPECT_EQ(fields[4].name, "tail");

    for(std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(fields[i].type().kind, type_kind::int32);
    }

    // Offsets must be strictly increasing
    for(std::size_t i = 1; i < 5; ++i) {
        EXPECT_TRUE(fields[i].offset > fields[i - 1].offset);
    }
}

TEST_CASE(default_value_and_literal) {
    EXPECT_EQ(virtual_schema<fx::DefaultLiteralStruct>::count, 3U);

    constexpr auto& fields = virtual_schema<fx::DefaultLiteralStruct>::fields;

    EXPECT_TRUE(fields[0].has_default);
    EXPECT_FALSE(fields[0].is_literal);

    EXPECT_TRUE(fields[1].is_literal);
    EXPECT_FALSE(fields[1].has_default);

    EXPECT_FALSE(fields[2].has_default);
    EXPECT_FALSE(fields[2].is_literal);
}

TEST_CASE(deny_unknown_default_false) {
    // deny_unknown_fields is resolved at the serde dispatch level, not on the
    // struct type itself. For regular reflectable structs, deny_unknown is always false.
    EXPECT_FALSE(virtual_schema<fx::SimpleStruct>::deny_unknown);
    EXPECT_FALSE(virtual_schema<fx::AnnotatedStruct>::deny_unknown);
}

TEST_CASE(nested_field_type_info) {
    // NestedStruct: items is vector<SimpleStruct>
    EXPECT_EQ(virtual_schema<fx::NestedStruct>::count, 1U);

    constexpr auto& fields = virtual_schema<fx::NestedStruct>::fields;
    EXPECT_EQ(fields[0].name, "items");
    EXPECT_EQ(fields[0].type().kind, type_kind::array);

    constexpr auto arr = static_cast<const array_type_info&>(fields[0].type());
    constexpr auto elem = arr.element();
    EXPECT_EQ(elem.kind, type_kind::structure);
}

TEST_CASE(tagged_field_type_info) {
    constexpr auto& fields = virtual_schema<fx::TaggedFieldStruct>::fields;
    EXPECT_EQ(fields.size(), 3U);

    constexpr auto ext = static_cast<const variant_type_info&>(fields[0].type());
    EXPECT_EQ(ext.tagging, tag_mode::external);
    EXPECT_EQ(ext.tag_field, "");
    EXPECT_EQ(ext.content_field, "");
    EXPECT_EQ(ext.alt_names.size(), 2U);
    EXPECT_EQ(ext.alt_names[0], "integer");
    EXPECT_EQ(ext.alt_names[1], "text");

    constexpr auto in = static_cast<const variant_type_info&>(fields[1].type());
    EXPECT_EQ(in.tagging, tag_mode::internal);
    EXPECT_EQ(in.tag_field, "kind");
    EXPECT_EQ(in.content_field, "");
    EXPECT_EQ(in.alt_names.size(), 2U);
    EXPECT_EQ(in.alt_names[0], "circle");
    EXPECT_EQ(in.alt_names[1], "rect");

    constexpr auto adj = static_cast<const variant_type_info&>(fields[2].type());
    EXPECT_EQ(adj.tagging, tag_mode::adjacent);
    EXPECT_EQ(adj.tag_field, "type");
    EXPECT_EQ(adj.content_field, "value");
    EXPECT_EQ(adj.alt_names.size(), 2U);
    EXPECT_EQ(adj.alt_names[0], "integer");
    EXPECT_EQ(adj.alt_names[1], "text");
}

};  // TEST_SUITE(virtual_schema_schema_attrs)

}  // namespace

}  // namespace kota::meta
