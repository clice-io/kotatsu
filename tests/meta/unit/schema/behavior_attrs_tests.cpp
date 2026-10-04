#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "fixtures/attrs.h"
#include "fixtures/tagged.h"
#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"

namespace kota::meta {

using kota::type_list_element_t;

namespace {

namespace fx = ::kota::test;

ZEST_SUITE(meta_schema_behavior_attrs) {

ZEST_CASE(skip_if_and_as) {
    ZSTATIC_EXPECT(virtual_schema<fx::BehaviorStruct>::count == 3U);

    constexpr auto& fields = virtual_schema<fx::BehaviorStruct>::fields;

    ZSTATIC_EXPECT(fields[0].name == "maybe");
    ZSTATIC_EXPECT(fields[1].name == "as_str");
    ZSTATIC_EXPECT(fields[2].name == "plain");

    // skip_if is not a behavior provider
    ZSTATIC_EXPECT(fields[0].has_skip_if);
    ZSTATIC_EXPECT(!fields[0].has_behavior);

    // as<string> is a behavior provider; encoded type becomes string
    ZSTATIC_EXPECT(fields[1].has_behavior);
    ZSTATIC_EXPECT(!fields[1].has_skip_if);
    ZSTATIC_EXPECT(fields[1].type().kind == type_kind::string);

    // plain field: no behavior flags
    ZSTATIC_EXPECT(!fields[2].has_skip_if);
    ZSTATIC_EXPECT(!fields[2].has_behavior);
}

ZEST_CASE(with_adapter_type_info) {
    constexpr auto& fields = virtual_schema<fx::WithReprStruct>::fields;

    // Adapter declares type = std::string
    ZSTATIC_EXPECT(fields[0].type().kind == type_kind::string);
    ZSTATIC_EXPECT(fields[0].has_behavior);

    // The slot keeps the raw field type; repr resolution is pinned by the
    // field's type_info kind above.
    using slots = virtual_schema<fx::WithReprStruct>::slots;
    using slot0 = type_list_element_t<0, slots>;
    ZEXPECT(zest::type_eq<slot0::raw_type, int>());

    // plain float is unaffected
    ZSTATIC_EXPECT(fields[1].type().kind == type_kind::float32);
    ZSTATIC_EXPECT(!fields[1].has_behavior);
}

ZEST_CASE(enum_string) {
    constexpr auto& fields = virtual_schema<fx::EnumStringStruct>::fields;

    // enum_string encodes through a non-owning view; the field's type_info
    // keeps the members, spelled through the policy.
    ZSTATIC_EXPECT(fields[0].type().kind == type_kind::enumeration);
    ZEXPECT(static_cast<const enum_type_info&>(fields[0].type()).rename != nullptr);
    ZSTATIC_EXPECT(fields[0].has_behavior);
    ZEXPECT(zest::type_eq<resolved_repr_t<decltype(fx::EnumStringStruct::color_field)>,
                          std::string_view>());

    // Verify slot types
    using slots = virtual_schema<fx::EnumStringStruct>::slots;
    using slot0 = type_list_element_t<0, slots>;
    ZEXPECT(zest::type_eq<slot0::raw_type, fx::Color>());

    // plain int is unaffected
    ZSTATIC_EXPECT(fields[1].type().kind == type_kind::int32);
    ZSTATIC_EXPECT(!fields[1].has_behavior);
}

ZEST_CASE(enum_string_through_a_chain) {
    // The members survive an as<> that resolves to the annotated enum.
    constexpr auto& fields = virtual_schema<fx::EnumStringThroughAs>::fields;
    ZSTATIC_EXPECT(fields[0].type().kind == type_kind::enumeration);
    ZEXPECT(static_cast<const enum_type_info&>(fields[0].type()).rename != nullptr);
}

ZEST_CASE(tagged_variant) {
    constexpr auto& fields = virtual_schema<fx::TaggedVariantStruct>::fields;
    ZSTATIC_EXPECT(fields[0].type().kind == type_kind::variant);

    // The slot keeps the raw variant type; the tagged<> spec itself is
    // asserted in slots_tests (tagged_variant_slot).
    using slots = virtual_schema<fx::TaggedVariantStruct>::slots;
    using slot0 = type_list_element_t<0, slots>;
    ZEXPECT(zest::type_eq<slot0::raw_type, std::variant<int, std::string>>());
}

ZEST_CASE(multi_attr_combination) {
    constexpr auto& fields = virtual_schema<fx::MultiAttrStruct>::fields;

    // opt_with_default: has_default=true, has_skip_if=true, has_behavior=false
    ZSTATIC_EXPECT(fields[0].has_default);
    ZSTATIC_EXPECT(fields[0].has_skip_if);
    ZSTATIC_EXPECT(!fields[0].has_behavior);

    // renamed_as: name="score", has_behavior=true, type->kind=string
    ZSTATIC_EXPECT(fields[1].name == "score");
    ZSTATIC_EXPECT(fields[1].has_behavior);
    ZSTATIC_EXPECT(fields[1].type().kind == type_kind::string);
}

ZEST_CASE(skip_if_combined_with_behavior) {
    // skip_if + as: both flags present
    {
        constexpr auto& fields = virtual_schema<fx::SkipIfAsStruct>::fields;
        ZSTATIC_EXPECT(fields[0].has_skip_if);
        ZSTATIC_EXPECT(fields[0].has_behavior);
        ZSTATIC_EXPECT(fields[0].type().kind == type_kind::string);
    }

    // skip_if + with: both flags present, encoded type = string (from adapter)
    {
        constexpr auto& fields = virtual_schema<fx::SkipIfWithStruct>::fields;
        ZSTATIC_EXPECT(fields[0].has_skip_if);
        ZSTATIC_EXPECT(fields[0].has_behavior);
        ZSTATIC_EXPECT(fields[0].type().kind == type_kind::string);
    }
}

};  // ZEST_SUITE(meta_schema_behavior_attrs)

}  // namespace

}  // namespace kota::meta
