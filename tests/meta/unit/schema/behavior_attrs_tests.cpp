#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "fixtures/attrs.h"
#include "fixtures/tagged.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/meta/attrs.h"
#include "kotatsu/meta/schema.h"

namespace kotatsu::meta {

using kotatsu::type_list_element_t;

namespace {

namespace fx = ::kotatsu::test;

ZEST_SUITE(meta_schema_behavior_attrs) {

ZEST_CASE(omit_if_and_as) {
    ZSTATIC_EXPECT(virtual_schema<fx::BehaviorStruct>::fields.size() == 3U);

    constexpr auto& fields = virtual_schema<fx::BehaviorStruct>::fields;

    ZSTATIC_EXPECT(fields[0].name == "maybe");
    ZSTATIC_EXPECT(fields[1].name == "as_str");
    ZSTATIC_EXPECT(fields[2].name == "plain");

    // omit_if is not a behavior provider
    ZSTATIC_EXPECT(fields[0].presence == presence_kind::omit_if);

    // as<string> is a behavior provider; encoded type becomes string
    ZSTATIC_EXPECT(fields[1].presence == presence_kind::required);
    ZSTATIC_EXPECT(fields[1].type().kind == type_kind::string);

    // plain field: no behavior flags
    ZSTATIC_EXPECT(fields[2].presence == presence_kind::required);
}

ZEST_CASE(with_adapter_type_info) {
    constexpr auto& fields = virtual_schema<fx::WithReprStruct>::fields;

    // Adapter declares type = std::string
    ZSTATIC_EXPECT(fields[0].type().kind == type_kind::string);

    // The slot keeps the raw field type; repr resolution is pinned by the
    // field's type_info kind above.
    using slots = virtual_schema<fx::WithReprStruct>::slots;
    using slot0 = type_list_element_t<0, slots>;
    ZEXPECT(zest::type_eq<slot0::raw_type, int>());

    // plain float is unaffected
    ZSTATIC_EXPECT(fields[1].type().kind == type_kind::float32);
}

ZEST_CASE(enum_names) {
    constexpr auto& fields = virtual_schema<fx::EnumNamesStruct>::fields;

    // enum_names encodes through a non-owning view; the field's type_info
    // keeps the members, as they are spelled.
    ZSTATIC_EXPECT(fields[0].type().kind == type_kind::enumeration);
    const auto& info = static_cast<const enum_type_info&>(fields[0].type());
    ZEXPECT(info.named);
    ZEXPECT(info.member_names[0] == "red");
    using resolved =
        decltype(detail::resolve_node<fx::Color, field_attrs_t<fx::EnumNamesStruct, 0>>());
    ZEXPECT(zest::type_eq<typename resolved::type, std::string_view>());

    // Verify slot types
    using slots = virtual_schema<fx::EnumNamesStruct>::slots;
    using slot0 = type_list_element_t<0, slots>;
    ZEXPECT(zest::type_eq<slot0::raw_type, fx::Color>());

    // plain int is unaffected
    ZSTATIC_EXPECT(fields[1].type().kind == type_kind::int32);
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

    // opt_omitted: presence by a predicate, description="Left out when empty."
    ZSTATIC_EXPECT(fields[0].presence == presence_kind::omit_if);
    ZSTATIC_EXPECT(fields[0].description == "Left out when empty.");

    // renamed_as: name="score", type->kind=string
    ZSTATIC_EXPECT(fields[1].name == "score");
    ZSTATIC_EXPECT(fields[1].type().kind == type_kind::string);
}

ZEST_CASE(omit_if_combined_with_behavior) {
    // omit_if + as: both present
    {
        constexpr auto& fields = virtual_schema<fx::OmitIfAsStruct>::fields;
        ZSTATIC_EXPECT(fields[0].presence == presence_kind::omit_if);
        ZSTATIC_EXPECT(fields[0].type().kind == type_kind::string);
    }

    // omit_if + with: both present, encoded type = string (from adapter)
    {
        constexpr auto& fields = virtual_schema<fx::OmitIfWithStruct>::fields;
        ZSTATIC_EXPECT(fields[0].presence == presence_kind::omit_if);
        ZSTATIC_EXPECT(fields[0].type().kind == type_kind::string);
    }
}

};  // ZEST_SUITE(meta_schema_behavior_attrs)

}  // namespace

}  // namespace kotatsu::meta
