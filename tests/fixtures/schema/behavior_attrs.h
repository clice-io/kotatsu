#pragma once

// Behavior-attr fixtures — skip_if / with / as / enum_string and combinations.

#include <optional>
#include <string>
#include <vector>

#include "fixtures/schema/enums.h"
#include "fixtures/schema/primitives.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"

namespace kota::meta::fixtures {

struct IntToStringAdapter {
    using wire_type = std::string;
};

// Adapter with no wire_type — falls back to raw type.
struct OpaqueAdapter {};

struct BytesAdapter {
    using wire_type = std::vector<std::byte>;
};

struct BehaviorStruct {
    annotation<std::optional<int>, behavior::skip_if<pred::optional_none>> maybe;
    annotation<int, behavior::as<std::string>> as_str;
    float plain;
};

struct WithWireTypeStruct {
    annotation<int, behavior::with<IntToStringAdapter>> converted;
    float plain;
};

struct WithNoWireTypeStruct {
    annotation<int, behavior::with<OpaqueAdapter>> opaque;
};

struct WithCompoundWireStruct {
    annotation<int, behavior::with<BytesAdapter>> chunk;
};

struct AsVectorStruct {
    annotation<int, behavior::as<std::vector<int>>> value;
};

struct AsStructStruct {
    annotation<int, behavior::as<SimpleStruct>> value;
};

struct AsOptionalStruct {
    annotation<int, behavior::as<std::optional<int>>> value;
};

struct EnumStringStruct {
    annotation<color, behavior::enum_string<rename_policy::identity>> color_field;
    int count;
};

struct EnumStringCamelStruct {
    annotation<color, behavior::enum_string<rename_policy::lower_camel>> color_field;
};

struct EnumStringUpperSnakeStruct {
    annotation<color, behavior::enum_string<rename_policy::upper_snake>> color_field;
};

struct SkipIfEmptyStringStruct {
    annotation<std::string, behavior::skip_if<pred::empty>> s;
};

struct SkipIfEmptyVectorStruct {
    annotation<std::vector<int>, behavior::skip_if<pred::empty>> xs;
};

struct SkipIfDefaultIntStruct {
    annotation<int, behavior::skip_if<pred::default_value>> x;
};

struct IsNegative {
    constexpr bool operator()(const int& v) const {
        return v < 0;
    }
};

struct SkipIfCustomStruct {
    annotation<int, behavior::skip_if<IsNegative>> maybe_negative;
};

struct MultiAttrStruct {
    annotation<std::optional<int>, attrs::default_value, behavior::skip_if<pred::optional_none>>
        opt_with_default;
    annotation<int, attrs::rename<"score">, behavior::as<std::string>> renamed_as;
};

struct SkipIfAsStruct {
    annotation<std::optional<std::string>,
               behavior::skip_if<pred::optional_none>,
               behavior::as<std::string>>
        field;
};

struct SkipIfWithStruct {
    annotation<std::optional<int>,
               behavior::skip_if<pred::optional_none>,
               behavior::with<IntToStringAdapter>>
        field;
};

}  // namespace kota::meta::fixtures
