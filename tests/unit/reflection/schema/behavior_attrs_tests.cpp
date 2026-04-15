#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "eventide/zest/zest.h"
#include "eventide/reflection/attrs.h"
#include "eventide/reflection/schema.h"

namespace eventide::refl {

using eventide::type_list_element_t;

namespace test_schema {

// -- skip_if + as --

struct BehaviorStruct {
    annotation<std::optional<int>, behavior::skip_if<pred::optional_none>> maybe;
    annotation<int, behavior::as<std::string>> as_str;
    float plain;
};

// -- with<Adapter> wire_type --

struct IntToStringAdapter {
    using wire_type = std::string;
};

struct WithWireTypeStruct {
    annotation<int, behavior::with<IntToStringAdapter>> converted;
    float plain;
};

// -- with<Adapter> no wire_type --

struct OpaqueAdapter {};

struct WithNoWireTypeStruct {
    annotation<int, behavior::with<OpaqueAdapter>> opaque;
};

// -- enum_string --

enum class color { red, green, blue };

struct EnumStringStruct {
    annotation<color, behavior::enum_string<rename_policy::identity>> color_field;
    int count;
};

// -- tagged variant --

struct TaggedVariantStruct {
    annotation<std::variant<int, std::string>, attrs::tagged<>> tv;
};

// -- multi-attribute combinations --

struct MultiAttrStruct {
    annotation<std::optional<int>, attrs::default_value, behavior::skip_if<pred::optional_none>>
        opt_with_default;
    annotation<int, attrs::rename<"score">, behavior::as<std::string>> renamed_as;
};

// -- skip_if + as combination --

struct SkipIfAsStruct {
    annotation<std::optional<std::string>,
               behavior::skip_if<pred::optional_none>,
               behavior::as<std::string>>
        field;
};

// -- skip_if + with combination --

struct SkipIfWithStruct {
    annotation<std::optional<int>,
               behavior::skip_if<pred::optional_none>,
               behavior::with<IntToStringAdapter>>
        field;
};

}  // namespace test_schema

namespace {

TEST_SUITE(virtual_schema_behavior_attrs) {

TEST_CASE(skip_if_and_as) {
    EXPECT_EQ(virtual_schema<test_schema::BehaviorStruct>::count, 3U);

    constexpr auto& fields = virtual_schema<test_schema::BehaviorStruct>::fields;

    EXPECT_EQ(fields[0].name, "maybe");
    EXPECT_EQ(fields[1].name, "as_str");
    EXPECT_EQ(fields[2].name, "plain");

    // skip_if is not a behavior provider
    EXPECT_TRUE(fields[0].has_skip_if);
    EXPECT_FALSE(fields[0].has_behavior);

    // as<string> is a behavior provider; wire type becomes string
    EXPECT_TRUE(fields[1].has_behavior);
    EXPECT_FALSE(fields[1].has_skip_if);
    EXPECT_EQ(fields[1].type->kind, type_kind::string);

    // plain field: no behavior flags
    EXPECT_FALSE(fields[2].has_skip_if);
    EXPECT_FALSE(fields[2].has_behavior);
}

TEST_CASE(with_wire_type) {
    constexpr auto& fields = virtual_schema<test_schema::WithWireTypeStruct>::fields;

    // Adapter declares wire_type = std::string
    EXPECT_EQ(fields[0].type->kind, type_kind::string);
    EXPECT_TRUE(fields[0].has_behavior);

    // Verify slot wire_type at compile time
    using slots = virtual_schema<test_schema::WithWireTypeStruct>::slots;
    using slot0 = type_list_element_t<0, slots>;
    EXPECT_TRUE((std::is_same_v<typename slot0::raw_type, int>));
    EXPECT_TRUE((std::is_same_v<typename slot0::wire_type, std::string>));

    // plain float is unaffected
    EXPECT_EQ(fields[1].type->kind, type_kind::float32);
    EXPECT_FALSE(fields[1].has_behavior);
}

TEST_CASE(with_no_wire_type) {
    constexpr auto& fields = virtual_schema<test_schema::WithNoWireTypeStruct>::fields;

    // Adapter has no wire_type, falls back to raw type (int -> int32)
    EXPECT_EQ(fields[0].type->kind, type_kind::int32);
    EXPECT_TRUE(fields[0].has_behavior);

    // Slot raw_type == wire_type when adapter lacks wire_type
    using slots = virtual_schema<test_schema::WithNoWireTypeStruct>::slots;
    using slot0 = type_list_element_t<0, slots>;
    EXPECT_TRUE((std::is_same_v<typename slot0::raw_type, int>));
    EXPECT_TRUE((std::is_same_v<typename slot0::wire_type, int>));
}

TEST_CASE(enum_string) {
    constexpr auto& fields = virtual_schema<test_schema::EnumStringStruct>::fields;

    // enum_string wire type is string_view -> kind is string
    EXPECT_EQ(fields[0].type->kind, type_kind::string);
    EXPECT_TRUE(fields[0].has_behavior);

    // Verify slot types
    using slots = virtual_schema<test_schema::EnumStringStruct>::slots;
    using slot0 = type_list_element_t<0, slots>;
    EXPECT_TRUE((std::is_same_v<typename slot0::raw_type, test_schema::color>));
    EXPECT_TRUE((std::is_same_v<typename slot0::wire_type, std::string_view>));

    // plain int is unaffected
    EXPECT_EQ(fields[1].type->kind, type_kind::int32);
    EXPECT_FALSE(fields[1].has_behavior);
}

TEST_CASE(tagged_variant) {
    constexpr auto& fields = virtual_schema<test_schema::TaggedVariantStruct>::fields;
    EXPECT_EQ(fields[0].type->kind, type_kind::variant);

    // tagged<> should appear in slot behavior attrs
    using slots = virtual_schema<test_schema::TaggedVariantStruct>::slots;
    using slot0 = type_list_element_t<0, slots>;
    EXPECT_TRUE((std::is_same_v<typename slot0::raw_type, std::variant<int, std::string>>));
    // wire_type stays as variant (tagged is a schema attr, not a type transform)
    EXPECT_TRUE((std::is_same_v<typename slot0::wire_type, std::variant<int, std::string>>));
}

TEST_CASE(multi_attr_combination) {
    constexpr auto& fields = virtual_schema<test_schema::MultiAttrStruct>::fields;

    // opt_with_default: has_default=true, has_skip_if=true, has_behavior=false
    EXPECT_TRUE(fields[0].has_default);
    EXPECT_TRUE(fields[0].has_skip_if);
    EXPECT_FALSE(fields[0].has_behavior);

    // renamed_as: name="score", has_behavior=true, type->kind=string
    EXPECT_EQ(fields[1].name, "score");
    EXPECT_TRUE(fields[1].has_behavior);
    EXPECT_EQ(fields[1].type->kind, type_kind::string);
}

TEST_CASE(skip_if_combined_with_behavior) {
    // skip_if + as: both flags present
    {
        constexpr auto& fields = virtual_schema<test_schema::SkipIfAsStruct>::fields;
        EXPECT_TRUE(fields[0].has_skip_if);
        EXPECT_TRUE(fields[0].has_behavior);
        EXPECT_EQ(fields[0].type->kind, type_kind::string);
    }

    // skip_if + with: both flags present, wire_type = string (from adapter)
    {
        constexpr auto& fields = virtual_schema<test_schema::SkipIfWithStruct>::fields;
        EXPECT_TRUE(fields[0].has_skip_if);
        EXPECT_TRUE(fields[0].has_behavior);
        EXPECT_EQ(fields[0].type->kind, type_kind::string);
    }
}

};  // TEST_SUITE(virtual_schema_behavior_attrs)

}  // namespace

}  // namespace eventide::refl
