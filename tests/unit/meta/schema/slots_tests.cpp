#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "fixtures/schema/behavior_attrs.h"
#include "fixtures/schema/enums.h"
#include "fixtures/schema/primitives.h"
#include "fixtures/schema/schema_attrs.h"
#include "fixtures/schema/tagged.h"
#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"

namespace kota::meta {

using kota::type_list_size_v;
using kota::type_list_element_t;

namespace {

namespace fx = ::kota::meta::fixtures;

TEST_SUITE(virtual_schema_slots) {

TEST_CASE(simple_struct_slots) {
    using slots = virtual_schema<fx::SimpleStruct>::slots;
    STATIC_EXPECT_EQ(type_list_size_v<slots>, 3U);

    using slot0 = type_list_element_t<0, slots>;
    using slot1 = type_list_element_t<1, slots>;
    using slot2 = type_list_element_t<2, slots>;

    // raw_type matches field types
    STATIC_EXPECT_EQ(kind_of<slot0::raw_type>(), type_kind::int32);
    STATIC_EXPECT_EQ(kind_of<slot1::raw_type>(), type_kind::string);
    STATIC_EXPECT_EQ(kind_of<slot2::raw_type>(), type_kind::float32);

    // wire_type == raw_type for plain fields
    EXPECT_TYPE_EQ(slot0::raw_type, slot0::wire_type);
    EXPECT_TYPE_EQ(slot1::raw_type, slot1::wire_type);
    EXPECT_TYPE_EQ(slot2::raw_type, slot2::wire_type);

    // attrs is empty tuple for plain fields
    EXPECT_TYPE_EQ(slot0::attrs, std::tuple<>);
}

TEST_CASE(skip_and_flatten_slot_counts) {
    // skip removes "internal", leaving 2
    using ann_slots = virtual_schema<fx::AnnotatedStruct>::slots;
    STATIC_EXPECT_EQ(type_list_size_v<ann_slots>, 2U);

    // flatten expands inner, yielding 4
    using outer_slots = virtual_schema<fx::Outer>::slots;
    STATIC_EXPECT_EQ(type_list_size_v<outer_slots>, 4U);

    // Verify flattened slot types: all int32
    using os0 = type_list_element_t<0, outer_slots>;
    using os1 = type_list_element_t<1, outer_slots>;
    using os2 = type_list_element_t<2, outer_slots>;
    using os3 = type_list_element_t<3, outer_slots>;
    STATIC_EXPECT_EQ(kind_of<os0::raw_type>(), type_kind::int32);
    STATIC_EXPECT_EQ(kind_of<os1::raw_type>(), type_kind::int32);
    STATIC_EXPECT_EQ(kind_of<os2::raw_type>(), type_kind::int32);
    STATIC_EXPECT_EQ(kind_of<os3::raw_type>(), type_kind::int32);
}

TEST_CASE(behavior_wire_types) {
    using slots = virtual_schema<fx::BehaviorStruct>::slots;

    // Field 0 (maybe): raw=optional<int>, wire=optional<int>, has skip_if attr
    using slot0 = type_list_element_t<0, slots>;
    EXPECT_TYPE_EQ(slot0::raw_type, std::optional<int>);
    EXPECT_TYPE_EQ(slot0::wire_type, std::optional<int>);
    STATIC_EXPECT_FALSE(std::is_same_v<slot0::attrs, std::tuple<>>);

    // Field 1 (as_str): raw=int, wire=std::string
    using slot1 = type_list_element_t<1, slots>;
    EXPECT_TYPE_EQ(slot1::raw_type, int);
    EXPECT_TYPE_EQ(slot1::wire_type, std::string);

    // Field 2 (plain): raw=wire=float, empty attrs
    using slot2 = type_list_element_t<2, slots>;
    STATIC_EXPECT_EQ(kind_of<slot2::raw_type>(), type_kind::float32);
    STATIC_EXPECT_EQ(kind_of<slot2::wire_type>(), type_kind::float32);
    EXPECT_TYPE_EQ(slot2::attrs, std::tuple<>);
}

TEST_CASE(enum_string_slot) {
    using slots = virtual_schema<fx::EnumStringStruct>::slots;
    using slot0 = type_list_element_t<0, slots>;

    EXPECT_TYPE_EQ(slot0::raw_type, fx::Color);
    EXPECT_TYPE_EQ(slot0::wire_type, std::string_view);
}

TEST_CASE(with_wire_type_slot) {
    using slots = virtual_schema<fx::WithWireTypeStruct>::slots;
    using slot0 = type_list_element_t<0, slots>;

    EXPECT_TYPE_EQ(slot0::raw_type, int);
    EXPECT_TYPE_EQ(slot0::wire_type, std::string);
}

TEST_CASE(tagged_variant_slot) {
    using slots = virtual_schema<fx::TaggedVariantStruct>::slots;
    using slot0 = type_list_element_t<0, slots>;

    // tagged<> is a schema attr that appears in behavior attrs filter
    EXPECT_TYPE_EQ(slot0::raw_type, std::variant<int, std::string>);
    EXPECT_TYPE_EQ(slot0::wire_type, std::variant<int, std::string>);
    // attrs is not empty (contains tagged<>)
    STATIC_EXPECT_FALSE(std::is_same_v<slot0::attrs, std::tuple<>>);
}

};  // TEST_SUITE(virtual_schema_slots)

}  // namespace

}  // namespace kota::meta
