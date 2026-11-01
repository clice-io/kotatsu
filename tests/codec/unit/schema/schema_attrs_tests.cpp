#include <optional>

#include "fixtures/attrs.h"
#include "fixtures/structs.h"
#include "fixtures/tagged.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/codec/attribute.h"
#include "kotatsu/codec/type_info.h"

namespace kotatsu::codec {

using meta::field_offset;
using meta::type_kind;

namespace {

namespace fx = ::kotatsu::test;

struct OmitDefaultsPolicy : extend<format> {
    constexpr static std::optional<presence_kind> presence_all = presence_kind::omit_if_default;
};

ZEST_SUITE(codec_schema_attrs) {

ZEST_CASE(simple_struct_fields) {
    ZSTATIC_EXPECT(virtual_schema<fx::SimpleStruct>::fields.size() == 3U);

    constexpr auto& fields = virtual_schema<fx::SimpleStruct>::fields;

    ZSTATIC_EXPECT(fields[0].name == "x");
    ZSTATIC_EXPECT(fields[1].name == "name");
    ZSTATIC_EXPECT(fields[2].name == "score");

    ZSTATIC_EXPECT(fields[0].type().kind == type_kind::int32);
    ZSTATIC_EXPECT(fields[1].type().kind == type_kind::string);
    ZSTATIC_EXPECT(fields[2].type().kind == type_kind::float32);

    // Offsets: first at 0, strictly increasing
    ZSTATIC_EXPECT(fields[0].offset == 0U);
    ZSTATIC_EXPECT(fields[1].offset > fields[0].offset);
    ZSTATIC_EXPECT(fields[2].offset > fields[1].offset);

    // Every field of a plain struct is required and has no aliases
    ZSTATIC_EXPECT(fields[0].presence == presence_kind::required);
    ZSTATIC_EXPECT(fields[0].aliases.size() == 0U);
    ZSTATIC_EXPECT(fields[1].presence == presence_kind::required);
    ZSTATIC_EXPECT(fields[1].aliases.size() == 0U);
    ZSTATIC_EXPECT(fields[2].presence == presence_kind::required);
    ZSTATIC_EXPECT(fields[2].aliases.size() == 0U);
}

ZEST_CASE(rename_and_skip) {
    // AnnotatedStruct: user_id(rename<"id">), internal(skip), value
    ZSTATIC_EXPECT(virtual_schema<fx::AnnotatedStruct>::fields.size() == 2U);

    constexpr auto& fields = virtual_schema<fx::AnnotatedStruct>::fields;

    ZSTATIC_EXPECT(fields[0].name == "id");
    ZSTATIC_EXPECT(fields[0].type().kind == type_kind::int32);

    ZSTATIC_EXPECT(fields[1].name == "value");
    ZSTATIC_EXPECT(fields[1].type().kind == type_kind::float32);
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
    ZSTATIC_EXPECT(virtual_schema<fx::Outer>::fields.size() == 4U);

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
    ZSTATIC_EXPECT(virtual_schema<fx::DeepOuter>::fields.size() == 5U);

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
    ZSTATIC_EXPECT(virtual_schema<fx::DefaultStruct>::fields.size() == 3U);

    constexpr auto& fields = virtual_schema<fx::DefaultStruct>::fields;

    ZSTATIC_EXPECT(fields[0].presence == presence_kind::optional);
    ZSTATIC_EXPECT(fields[1].presence == presence_kind::required);
    ZSTATIC_EXPECT(fields[2].presence == presence_kind::required);
}

ZEST_CASE(presence_all_of_the_format_reaches_every_field) {
    // Every field takes the format's presence, in the structs nested inside
    // too, but one that gives its own: DefaultStruct's first field is
    // optional of its own.
    constexpr auto& fields = virtual_schema<fx::DefaultStruct, OmitDefaultsPolicy>::fields;
    ZSTATIC_EXPECT(fields[0].presence == presence_kind::optional);
    ZSTATIC_EXPECT(fields[1].presence == presence_kind::omit_if_default);
    ZSTATIC_EXPECT(fields[2].presence == presence_kind::omit_if_default);

    constexpr auto& items = virtual_schema<fx::NestedStruct, OmitDefaultsPolicy>::fields;
    constexpr auto& element = static_cast<const array_type_info&>(items[0].type()).element();
    constexpr auto& simple = static_cast<const struct_type_info&>(element);
    ZSTATIC_EXPECT(simple.fields[0].presence == presence_kind::omit_if_default);
}

ZEST_CASE(deny_unknown_default_false) {
    // deny_unknown_fields is resolved at the serde dispatch level, not on the
    // struct type itself. For regular reflectable structs, deny_unknown is always false.
    ZSTATIC_EXPECT(!virtual_schema<fx::SimpleStruct>::deny_unknown);
    ZSTATIC_EXPECT(!virtual_schema<fx::AnnotatedStruct>::deny_unknown);
}

ZEST_CASE(nested_field_type_info) {
    // NestedStruct: items is vector<SimpleStruct>
    ZSTATIC_EXPECT(virtual_schema<fx::NestedStruct>::fields.size() == 1U);

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

};  // ZEST_SUITE(codec_schema_attrs)

}  // namespace

}  // namespace kotatsu::codec
