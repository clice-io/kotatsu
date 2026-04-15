#include <cstddef>
#include <string>
#include <variant>
#include <vector>

#include "eventide/zest/zest.h"
#include "eventide/reflection/attrs.h"
#include "eventide/reflection/schema.h"

namespace eventide::refl {

namespace test_schema {

// -- rename + skip --

struct AnnotatedStruct {
    annotation<int, attrs::rename<"id">> user_id;
    annotation<std::string, attrs::skip> internal;
    float value;
};

// -- alias --

struct AliasStruct {
    annotation<int, attrs::alias<"user_id", "userId">> id;
    std::string name;
};

// -- flatten --

struct Inner {
    int a;
    int b;
};

struct Outer {
    int x;
    annotation<Inner, attrs::flatten> inner;
    int y;
};

// -- deep flatten (flatten of flatten) --

struct DeepInner {
    int p;
    int q;
};

struct Middle {
    int m;
    annotation<DeepInner, attrs::flatten> deep;
};

struct DeepOuter {
    int head;
    annotation<Middle, attrs::flatten> mid;
    int tail;
};

// -- default_value + literal --

struct DefaultLiteralStruct {
    annotation<int, attrs::default_value> with_default;
    annotation<std::string, attrs::literal<"v1">> version;
    int plain;
};

// -- simple struct (for basic field verification) --

struct SimpleStruct {
    int x;
    std::string name;
    float score;
};

// -- nested struct for field_info type tree --

struct NestedStruct {
    std::vector<SimpleStruct> items;
};

struct TaggedCircle {
    int radius;
};

struct TaggedRect {
    int width;
    int height;
};

using ExternalTagged =
    annotation<std::variant<int, std::string>, attrs::externally_tagged::names<"integer", "text">>;

using InternalTagged = annotation<std::variant<TaggedCircle, TaggedRect>,
                                  attrs::internally_tagged<"kind">::names<"circle", "rect">>;

using AdjacentTagged =
    annotation<std::variant<int, std::string>,
               attrs::adjacently_tagged<"type", "value">::names<"integer", "text">>;

struct TaggedFieldStruct {
    ExternalTagged ext;
    InternalTagged in;
    AdjacentTagged adj;
};

}  // namespace test_schema

namespace {

// ----- rename, skip, alias -----

TEST_SUITE(virtual_schema_schema_attrs) {

TEST_CASE(simple_struct_fields) {
    EXPECT_EQ(virtual_schema<test_schema::SimpleStruct>::count, 3U);

    constexpr auto& fields = virtual_schema<test_schema::SimpleStruct>::fields;

    EXPECT_EQ(fields[0].name, "x");
    EXPECT_EQ(fields[1].name, "name");
    EXPECT_EQ(fields[2].name, "score");

    EXPECT_EQ(fields[0].type->kind, type_kind::int32);
    EXPECT_EQ(fields[1].type->kind, type_kind::string);
    EXPECT_EQ(fields[2].type->kind, type_kind::float32);

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
    EXPECT_EQ(virtual_schema<test_schema::AnnotatedStruct>::count, 2U);

    constexpr auto& fields = virtual_schema<test_schema::AnnotatedStruct>::fields;

    EXPECT_EQ(fields[0].name, "id");
    EXPECT_EQ(fields[0].type->kind, type_kind::int32);
    EXPECT_EQ(fields[0].physical_index, 0U);

    EXPECT_EQ(fields[1].name, "value");
    EXPECT_EQ(fields[1].type->kind, type_kind::float32);
    EXPECT_EQ(fields[1].physical_index, 2U);
}

TEST_CASE(alias) {
    constexpr auto& fields = virtual_schema<test_schema::AliasStruct>::fields;

    EXPECT_EQ(fields[0].aliases.size(), 2U);
    EXPECT_EQ(fields[0].aliases[0], "user_id");
    EXPECT_EQ(fields[0].aliases[1], "userId");

    // Plain field has no aliases
    EXPECT_EQ(fields[1].aliases.size(), 0U);
}

TEST_CASE(flatten) {
    // Outer: x(1) + Inner{a,b}(2) + y(1) = 4 fields
    EXPECT_EQ(virtual_schema<test_schema::Outer>::count, 4U);

    constexpr auto& fields = virtual_schema<test_schema::Outer>::fields;

    EXPECT_EQ(fields[0].name, "x");
    EXPECT_EQ(fields[1].name, "a");
    EXPECT_EQ(fields[2].name, "b");
    EXPECT_EQ(fields[3].name, "y");

    for(std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(fields[i].type->kind, type_kind::int32);
    }

    // Offsets: strictly increasing, flattened offsets match outer+inner layout
    EXPECT_EQ(fields[0].offset, 0U);
    EXPECT_TRUE(fields[1].offset > fields[0].offset);
    EXPECT_TRUE(fields[2].offset > fields[1].offset);
    EXPECT_TRUE(fields[3].offset > fields[2].offset);

    constexpr auto outer_inner_offset = field_offset<test_schema::Outer>(1);
    constexpr auto inner_a_offset = field_offset<test_schema::Inner>(0);
    constexpr auto inner_b_offset = field_offset<test_schema::Inner>(1);
    EXPECT_EQ(fields[1].offset, outer_inner_offset + inner_a_offset);
    EXPECT_EQ(fields[2].offset, outer_inner_offset + inner_b_offset);
}

TEST_CASE(deep_flatten) {
    // DeepOuter: head + Middle{m, DeepInner{p,q}} + tail = 5 fields
    EXPECT_EQ(virtual_schema<test_schema::DeepOuter>::count, 5U);

    constexpr auto& fields = virtual_schema<test_schema::DeepOuter>::fields;

    EXPECT_EQ(fields[0].name, "head");
    EXPECT_EQ(fields[1].name, "m");
    EXPECT_EQ(fields[2].name, "p");
    EXPECT_EQ(fields[3].name, "q");
    EXPECT_EQ(fields[4].name, "tail");

    for(std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(fields[i].type->kind, type_kind::int32);
    }

    // Offsets must be strictly increasing
    for(std::size_t i = 1; i < 5; ++i) {
        EXPECT_TRUE(fields[i].offset > fields[i - 1].offset);
    }
}

TEST_CASE(default_value_and_literal) {
    EXPECT_EQ(virtual_schema<test_schema::DefaultLiteralStruct>::count, 3U);

    constexpr auto& fields = virtual_schema<test_schema::DefaultLiteralStruct>::fields;

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
    EXPECT_FALSE(virtual_schema<test_schema::SimpleStruct>::deny_unknown);
    EXPECT_FALSE(virtual_schema<test_schema::AnnotatedStruct>::deny_unknown);
}

TEST_CASE(nested_field_type_info) {
    // NestedStruct: items is vector<SimpleStruct>
    EXPECT_EQ(virtual_schema<test_schema::NestedStruct>::count, 1U);

    constexpr auto& fields = virtual_schema<test_schema::NestedStruct>::fields;
    EXPECT_EQ(fields[0].name, "items");
    EXPECT_EQ(fields[0].type->kind, type_kind::array);

    auto* arr = static_cast<const array_type_info*>(fields[0].type);
    EXPECT_EQ(arr->element->kind, type_kind::structure);
}

TEST_CASE(tagged_field_type_info) {
    constexpr auto& fields = virtual_schema<test_schema::TaggedFieldStruct>::fields;
    EXPECT_EQ(fields.size(), 3U);

    auto* ext = static_cast<const variant_type_info*>(fields[0].type);
    EXPECT_EQ(ext->tagging, tag_mode::external);
    EXPECT_EQ(ext->tag_field, "");
    EXPECT_EQ(ext->content_field, "");
    EXPECT_EQ(ext->alt_names.size(), 2U);
    EXPECT_EQ(ext->alt_names[0], "integer");
    EXPECT_EQ(ext->alt_names[1], "text");

    auto* in = static_cast<const variant_type_info*>(fields[1].type);
    EXPECT_EQ(in->tagging, tag_mode::internal);
    EXPECT_EQ(in->tag_field, "kind");
    EXPECT_EQ(in->content_field, "");
    EXPECT_EQ(in->alt_names.size(), 2U);
    EXPECT_EQ(in->alt_names[0], "circle");
    EXPECT_EQ(in->alt_names[1], "rect");

    auto* adj = static_cast<const variant_type_info*>(fields[2].type);
    EXPECT_EQ(adj->tagging, tag_mode::adjacent);
    EXPECT_EQ(adj->tag_field, "type");
    EXPECT_EQ(adj->content_field, "value");
    EXPECT_EQ(adj->alt_names.size(), 2U);
    EXPECT_EQ(adj->alt_names[0], "integer");
    EXPECT_EQ(adj->alt_names[1], "text");
}

};  // TEST_SUITE(virtual_schema_schema_attrs)

}  // namespace

}  // namespace eventide::refl
