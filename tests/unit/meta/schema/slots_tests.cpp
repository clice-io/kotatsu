#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

#include "fixtures/schema/behavior_attrs.h"
#include "fixtures/schema/enums.h"
#include "fixtures/schema/primitives.h"
#include "fixtures/schema/schema_attrs.h"
#include "fixtures/schema/tagged.h"
#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/repr.h"
#include "kota/meta/schema.h"

namespace kota_slots_repr_test {

struct RepId {
    std::uint32_t v = 0;
};

struct DynBlob {};

struct ReprStruct {
    RepId id;
    DynBlob blob;
    int plain = 0;
};

}  // namespace kota_slots_repr_test

namespace kota::meta {

template <>
struct repr<kota_slots_repr_test::RepId> {
    using type = std::uint32_t;
};

template <>
struct repr<kota_slots_repr_test::DynBlob> {
    using type = dynamic;
};

}  // namespace kota::meta

namespace kota::meta {

using kota::type_list_size_v;
using kota::type_list_element_t;

namespace {

namespace fx = ::kota::meta::fixtures;

ZEST_SUITE(virtual_schema_slots){

    ZEST_CASE(simple_struct_slots){using slots = virtual_schema<fx::SimpleStruct>::slots;
STATIC_EXPECT(type_list_size_v<slots> == 3U);

using slot0 = type_list_element_t<0, slots>;
using slot1 = type_list_element_t<1, slots>;
using slot2 = type_list_element_t<2, slots>;

// raw_type matches field types
STATIC_EXPECT(kind_of<slot0::raw_type>() == type_kind::int32);
STATIC_EXPECT(kind_of<slot1::raw_type>() == type_kind::string);
STATIC_EXPECT(kind_of<slot2::raw_type>() == type_kind::float32);

// attrs is empty tuple for plain fields
EXPECT(zest::type_eq<slot0::attrs, std::tuple<>>());

}  // namespace

ZEST_CASE(skip_and_flatten_slot_counts) {
    // skip removes "internal", leaving 2
    using ann_slots = virtual_schema<fx::AnnotatedStruct>::slots;
    STATIC_EXPECT(type_list_size_v<ann_slots> == 2U);

    // flatten expands inner, yielding 4
    using outer_slots = virtual_schema<fx::Outer>::slots;
    STATIC_EXPECT(type_list_size_v<outer_slots> == 4U);

    // Verify flattened slot types: all int32
    using os0 = type_list_element_t<0, outer_slots>;
    using os1 = type_list_element_t<1, outer_slots>;
    using os2 = type_list_element_t<2, outer_slots>;
    using os3 = type_list_element_t<3, outer_slots>;
    STATIC_EXPECT(kind_of<os0::raw_type>() == type_kind::int32);
    STATIC_EXPECT(kind_of<os1::raw_type>() == type_kind::int32);
    STATIC_EXPECT(kind_of<os2::raw_type>() == type_kind::int32);
    STATIC_EXPECT(kind_of<os3::raw_type>() == type_kind::int32);
}

ZEST_CASE(behavior_slot_attrs) {
    using slots = virtual_schema<fx::BehaviorStruct>::slots;

    // Field 0 (maybe): raw=optional<int>, has skip_if attr
    using slot0 = type_list_element_t<0, slots>;
    EXPECT(zest::type_eq<slot0::raw_type, std::optional<int>>());
    STATIC_EXPECT(!std::is_same_v<slot0::attrs, std::tuple<>>);

    // Field 1 (as_str): the slot keeps the raw type; the behavior transform
    // lives in the field's type_info kind, not the slot.
    using slot1 = type_list_element_t<1, slots>;
    EXPECT(zest::type_eq<slot1::raw_type, int>());

    // Field 2 (plain): empty attrs
    using slot2 = type_list_element_t<2, slots>;
    STATIC_EXPECT(kind_of<slot2::raw_type>() == type_kind::float32);
    EXPECT(zest::type_eq<slot2::attrs, std::tuple<>>());
}

ZEST_CASE(repr_field_type_info) {
    namespace ns = ::kota_slots_repr_test;
    using slots = virtual_schema<ns::ReprStruct>::slots;

    // The slot keeps the raw field type; repr substitution shows up in the
    // field's type_info (a dynamic repr degrades to type_kind::any).
    using slot0 = type_list_element_t<0, slots>;
    EXPECT(zest::type_eq<slot0::raw_type, ns::RepId>());

    constexpr auto& fields = virtual_schema<ns::ReprStruct>::fields;
    STATIC_EXPECT(fields[0].type().kind == type_kind::uint32);
    STATIC_EXPECT(fields[1].type().kind == type_kind::any);
    STATIC_EXPECT(fields[2].type().kind == type_kind::int32);
}

ZEST_CASE(tagged_variant_slot) {
    using slots = virtual_schema<fx::TaggedVariantStruct>::slots;
    using slot0 = type_list_element_t<0, slots>;

    // tagged<> is a schema attr that appears in behavior attrs filter
    EXPECT(zest::type_eq<slot0::raw_type, std::variant<int, std::string>>());
    // attrs is not empty (contains tagged<>)
    STATIC_EXPECT(!std::is_same_v<slot0::attrs, std::tuple<>>);
}

};  // namespace kota::meta

}  // namespace

}  // namespace kota::meta
