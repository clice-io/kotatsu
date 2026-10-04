#include "fixtures/attrs.h"
#include "fixtures/structs.h"
#include "fixtures/tagged.h"
#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"

namespace kota::meta {

namespace {

namespace fx = ::kota::test;

struct DefaultedPolicy {
    constexpr static bool defaulted_fields = true;
};

ZEST_SUITE(meta_schema_attrs) {

ZEST_CASE(simple_struct_fields) {
    ZSTATIC_EXPECT(virtual_schema<fx::SimpleStruct>::count == 3U);

    constexpr auto& fields = virtual_schema<fx::SimpleStruct>::fields;

    ZSTATIC_EXPECT(fields[0].name == "x");
    ZSTATIC_EXPECT(fields[1].name == "name");
    ZSTATIC_EXPECT(fields[2].name == "score");

    ZSTATIC_EXPECT(fields[0].type().kind == type_kind::int32);
    ZSTATIC_EXPECT(fields[1].type().kind == type_kind::string);
    ZSTATIC_EXPECT(fields[2].type().kind == type_kind::float32);

    ZSTATIC_EXPECT(fields[0].physical_index == 0U);
    ZSTATIC_EXPECT(fields[1].physical_index == 1U);
    ZSTATIC_EXPECT(fields[2].physical_index == 2U);

    // Offsets: first at 0, strictly increasing
    ZSTATIC_EXPECT(fields[0].offset == 0U);
    ZSTATIC_EXPECT(fields[1].offset > fields[0].offset);
    ZSTATIC_EXPECT(fields[2].offset > fields[1].offset);

    // All flags false for plain struct
    ZSTATIC_EXPECT(!fields[0].has_default);
    ZSTATIC_EXPECT(!fields[0].has_skip_if);
    ZSTATIC_EXPECT(!fields[0].has_behavior);
    ZSTATIC_EXPECT(fields[0].aliases.size() == 0U);
    ZSTATIC_EXPECT(!fields[1].has_default);
    ZSTATIC_EXPECT(!fields[1].has_skip_if);
    ZSTATIC_EXPECT(!fields[1].has_behavior);
    ZSTATIC_EXPECT(fields[1].aliases.size() == 0U);
    ZSTATIC_EXPECT(!fields[2].has_default);
    ZSTATIC_EXPECT(!fields[2].has_skip_if);
    ZSTATIC_EXPECT(!fields[2].has_behavior);
    ZSTATIC_EXPECT(fields[2].aliases.size() == 0U);
}

ZEST_CASE(rename_and_skip) {
    // AnnotatedStruct: user_id(rename<"id">), internal(skip), value
    ZSTATIC_EXPECT(virtual_schema<fx::AnnotatedStruct>::count == 2U);

    constexpr auto& fields = virtual_schema<fx::AnnotatedStruct>::fields;

    ZSTATIC_EXPECT(fields[0].name == "id");
    ZSTATIC_EXPECT(fields[0].type().kind == type_kind::int32);
    ZSTATIC_EXPECT(fields[0].physical_index == 0U);

    ZSTATIC_EXPECT(fields[1].name == "value");
    ZSTATIC_EXPECT(fields[1].type().kind == type_kind::float32);
    ZSTATIC_EXPECT(fields[1].physical_index == 2U);
}

ZEST_CASE(alias) {
    constexpr auto& fields = virtual_schema<fx::AliasStruct>::fields;

    ZSTATIC_EXPECT(fields[0].aliases.size() == 2U);
    ZSTATIC_EXPECT(fields[0].aliases[0] == "user_id");
    ZSTATIC_EXPECT(fields[0].aliases[1] == "userId");

    // Plain field has no aliases
    ZSTATIC_EXPECT(fields[1].aliases.size() == 0U);
}

ZEST_CASE(flatten) {
    // Outer: x(1) + Inner{a,b}(2) + y(1) = 4 fields
    ZSTATIC_EXPECT(virtual_schema<fx::Outer>::count == 4U);

    constexpr auto& fields = virtual_schema<fx::Outer>::fields;

    ZSTATIC_EXPECT(fields[0].name == "x");
    ZSTATIC_EXPECT(fields[1].name == "a");
    ZSTATIC_EXPECT(fields[2].name == "b");
    ZSTATIC_EXPECT(fields[3].name == "y");

    ZSTATIC_EXPECT(fields[0].type().kind == type_kind::int32);
    ZSTATIC_EXPECT(fields[1].type().kind == type_kind::int32);
    ZSTATIC_EXPECT(fields[2].type().kind == type_kind::int32);
    ZSTATIC_EXPECT(fields[3].type().kind == type_kind::int32);

    // Offsets: strictly increasing, flattened offsets match outer+inner layout
    ZSTATIC_EXPECT(fields[0].offset == 0U);
    ZSTATIC_EXPECT(fields[1].offset > fields[0].offset);
    ZSTATIC_EXPECT(fields[2].offset > fields[1].offset);
    ZSTATIC_EXPECT(fields[3].offset > fields[2].offset);

    constexpr auto outer_inner_offset = field_offset<fx::Outer>(1);
    constexpr auto inner_a_offset = field_offset<fx::Inner>(0);
    constexpr auto inner_b_offset = field_offset<fx::Inner>(1);
    ZSTATIC_EXPECT(fields[1].offset == outer_inner_offset + inner_a_offset);
    ZSTATIC_EXPECT(fields[2].offset == outer_inner_offset + inner_b_offset);
}

ZEST_CASE(deep_flatten) {
    // DeepOuter: head + Middle{m, DeepInner{p,q}} + tail = 5 fields
    ZSTATIC_EXPECT(virtual_schema<fx::DeepOuter>::count == 5U);

    constexpr auto& fields = virtual_schema<fx::DeepOuter>::fields;

    ZSTATIC_EXPECT(fields[0].name == "head");
    ZSTATIC_EXPECT(fields[1].name == "m");
    ZSTATIC_EXPECT(fields[2].name == "p");
    ZSTATIC_EXPECT(fields[3].name == "q");
    ZSTATIC_EXPECT(fields[4].name == "tail");

    ZSTATIC_EXPECT(fields[0].type().kind == type_kind::int32);
    ZSTATIC_EXPECT(fields[1].type().kind == type_kind::int32);
    ZSTATIC_EXPECT(fields[2].type().kind == type_kind::int32);
    ZSTATIC_EXPECT(fields[3].type().kind == type_kind::int32);
    ZSTATIC_EXPECT(fields[4].type().kind == type_kind::int32);

    // Offsets must be strictly increasing
    ZSTATIC_EXPECT(fields[1].offset > fields[0].offset);
    ZSTATIC_EXPECT(fields[2].offset > fields[1].offset);
    ZSTATIC_EXPECT(fields[3].offset > fields[2].offset);
    ZSTATIC_EXPECT(fields[4].offset > fields[3].offset);
}

ZEST_CASE(default_value) {
    ZSTATIC_EXPECT(virtual_schema<fx::DefaultStruct>::count == 3U);

    constexpr auto& fields = virtual_schema<fx::DefaultStruct>::fields;

    ZSTATIC_EXPECT(fields[0].has_default);
    ZSTATIC_EXPECT(!fields[1].has_default);
    ZSTATIC_EXPECT(!fields[2].has_default);
}

ZEST_CASE(defaulted_fields_config_defaults_every_field) {
    // Every field has a default, in the structs nested inside too.
    // DefaultStruct's first field is defaulted of its own.
    constexpr auto& fields = virtual_schema<fx::DefaultStruct, DefaultedPolicy>::fields;
    ZSTATIC_EXPECT(fields[1].has_default);
    ZSTATIC_EXPECT(fields[2].has_default);

    constexpr auto& items = virtual_schema<fx::NestedStruct, DefaultedPolicy>::fields;
    constexpr auto& element = static_cast<const array_type_info&>(items[0].type()).element();
    constexpr auto& simple = static_cast<const struct_type_info&>(element);
    ZSTATIC_EXPECT(simple.fields[0].has_default);
}

ZEST_CASE(deny_unknown_default_false) {
    // deny_unknown_fields is resolved at the serde dispatch level, not on the
    // struct type itself. For regular reflectable structs, deny_unknown is always false.
    ZSTATIC_EXPECT(!virtual_schema<fx::SimpleStruct>::deny_unknown);
    ZSTATIC_EXPECT(!virtual_schema<fx::AnnotatedStruct>::deny_unknown);
}

ZEST_CASE(nested_field_type_info) {
    // NestedStruct: items is vector<SimpleStruct>
    ZSTATIC_EXPECT(virtual_schema<fx::NestedStruct>::count == 1U);

    constexpr auto& fields = virtual_schema<fx::NestedStruct>::fields;
    ZSTATIC_EXPECT(fields[0].name == "items");
    ZSTATIC_EXPECT(fields[0].type().kind == type_kind::array);

    constexpr auto arr = static_cast<const array_type_info&>(fields[0].type());
    constexpr auto elem = arr.element();
    ZSTATIC_EXPECT(elem.kind == type_kind::structure);
}

ZEST_CASE(tagged_field_type_info) {
    constexpr auto& fields = virtual_schema<fx::TaggedFieldStruct>::fields;
    ZSTATIC_EXPECT(fields.size() == 3U);

    constexpr auto ext = static_cast<const variant_type_info&>(fields[0].type());
    ZSTATIC_EXPECT(ext.tagging == tag_mode::external);
    ZSTATIC_EXPECT(ext.tag_field == "");
    ZSTATIC_EXPECT(ext.content_field == "");
    ZSTATIC_EXPECT(ext.alt_names.size() == 2U);
    ZSTATIC_EXPECT(ext.alt_names[0] == "integer");
    ZSTATIC_EXPECT(ext.alt_names[1] == "text");

    constexpr auto in = static_cast<const variant_type_info&>(fields[1].type());
    ZSTATIC_EXPECT(in.tagging == tag_mode::internal);
    ZSTATIC_EXPECT(in.tag_field == "kind");
    ZSTATIC_EXPECT(in.content_field == "");
    ZSTATIC_EXPECT(in.alt_names.size() == 2U);
    ZSTATIC_EXPECT(in.alt_names[0] == "circle");
    ZSTATIC_EXPECT(in.alt_names[1] == "rect");

    constexpr auto adj = static_cast<const variant_type_info&>(fields[2].type());
    ZSTATIC_EXPECT(adj.tagging == tag_mode::adjacent);
    ZSTATIC_EXPECT(adj.tag_field == "type");
    ZSTATIC_EXPECT(adj.content_field == "value");
    ZSTATIC_EXPECT(adj.alt_names.size() == 2U);
    ZSTATIC_EXPECT(adj.alt_names[0] == "integer");
    ZSTATIC_EXPECT(adj.alt_names[1] == "text");
}

};  // ZEST_SUITE(meta_schema_attrs)

}  // namespace

}  // namespace kota::meta
