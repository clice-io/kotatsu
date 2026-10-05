#include <cstdint>
#include <limits>
#include <string>
#include <variant>

#include "codec/harness/fixtures/repr.h"
#include "codec/harness/fixtures/structs.h"
#include "codec/json/harness/schema.h"
#include "fixtures/repr.h"
#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/codec/json/schema.h"

namespace kota::meta {

namespace {

using test::color_i8;
using test::with_enum;
using test::casing_child;
using test::root_external_variant;

namespace json = kota::codec::json;

ZEST_SUITE(codec_json_schema_encoder) {

struct string_enum_config {
    [[maybe_unused]] constexpr static auto enum_repr = codec::enum_repr::String;
};

ZEST_CASE(enum_names_under_string_config) {
    const auto result = json::schema_string<color_i8, string_enum_config>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("enum":["red","green","blue"]})");
}

ZEST_CASE(unnamed_enum_value_rejected_under_string_config) {
    // A representable value without a reflected member name has no string
    // spelling: the encoder rejects it, so the schema's enum list of
    // reflected names stays exhaustive.
    const auto encoded = json::to_string<string_enum_config>(static_cast<color_i8>(42));
    ZEXPECT(!encoded);
}

ZEST_CASE(schema_agrees_with_encoder_on_enums) {
    // Under the default config the encoder emits the numeric value, so the
    // schema constrains the same numeric form.
    const auto encoded = json::to_string(with_enum{.c = color_i8::green, .name = "g"});
    ZASSERT(encoded);
    ZEXPECT(zest::contains(*encoded, R"("c":1)"));

    const auto schema = json::schema_string<with_enum>().value();
    ZEXPECT(zest::contains(schema, R"("c":{"type":"integer","minimum":-128,"maximum":127})"));
}

struct renamed_enum_config {
    [[maybe_unused]] constexpr static auto enum_repr = codec::enum_repr::String;
    using enum_rename = naming::rename_policy::upper_snake;
};

ZEST_CASE(schema_agrees_with_encoder_on_enum_rename) {
    // The schema lists the spellings the encoder writes under the same
    // config, not the raw reflected names.
    const auto encoded = json::to_string<renamed_enum_config>(color_i8::green);
    ZASSERT(encoded);
    ZEXPECT(*encoded == R"("GREEN")");

    const auto result = json::schema_string<color_i8, renamed_enum_config>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("enum":["RED","GREEN","BLUE"]})");
}

struct nan_null_config {
    [[maybe_unused]] constexpr static auto nan_repr = codec::nan_repr::Null;
};

struct nan_string_config {
    [[maybe_unused]] constexpr static auto nan_repr = codec::nan_repr::String;
};

ZEST_CASE(schema_agrees_with_encoder_on_nan_passthrough) {
    // The default Passthrough forwards the non-finite value to the writer,
    // whose only JSON spelling for it is null — the schema must admit that.
    const auto encoded = json::to_string(std::numeric_limits<double>::quiet_NaN());
    ZASSERT(encoded);
    ZEXPECT(*encoded == "null");

    const auto result = json::schema_string<double>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("anyOf":[{"type":"number"},{"type":"null"}]})");
}

ZEST_CASE(schema_agrees_with_encoder_on_nan_null) {
    const auto encoded = json::to_string<nan_null_config>(std::numeric_limits<double>::quiet_NaN());
    ZASSERT(encoded);
    ZEXPECT(*encoded == "null");

    const auto result = json::schema_string<double, nan_null_config>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("anyOf":[{"type":"number"},{"type":"null"}]})");
}

ZEST_CASE(schema_agrees_with_encoder_on_nan_string) {
    const auto encoded = json::to_string<nan_string_config>(std::numeric_limits<float>::infinity());
    ZASSERT(encoded);
    ZEXPECT(*encoded == R"("Infinity")");

    const auto result = json::schema_string<float, nan_string_config>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("anyOf":[{"type":"number"},)"
                      R"({"enum":["NaN","Infinity","-Infinity"]}]})");
}

struct nan_error_config {
    [[maybe_unused]] constexpr static auto nan_repr = codec::nan_repr::Error;
};

ZEST_CASE(schema_agrees_with_encoder_on_long_double_overflow) {
    // A finite long double beyond double's range narrows to infinity in the
    // document, so the nan_repr policy judges the narrowed value: String
    // spells it, Error rejects it — never a null the schema does not admit.
    constexpr long double big = std::numeric_limits<long double>::max();
    if constexpr(big > static_cast<long double>(std::numeric_limits<double>::max())) {
        const auto spelled = json::to_string<nan_string_config>(big);
        ZASSERT(spelled);
        ZEXPECT(*spelled == R"("Infinity")");

        const auto negative = json::to_string<nan_string_config>(-big);
        ZASSERT(negative);
        ZEXPECT(*negative == R"("-Infinity")");

        ZEXPECT(!json::to_string<nan_error_config>(big).has_value());
    } else {
        // long double is double: the value stays a finite number.
        ZEXPECT(json::to_string<nan_error_config>(big).has_value());
    }
}

struct non_hr_config {
    [[maybe_unused]] constexpr static bool human_readable = false;
};

ZEST_CASE(schema_agrees_with_encoder_on_non_human_readable) {
    // A non-human-readable config bypasses tagging and encodes the underlying
    // variant, so the schema describes the untagged alternatives.
    const auto encoded = json::to_string<non_hr_config>(root_external_variant{7});
    ZASSERT(encoded);
    ZEXPECT(*encoded == "7");

    const auto result = json::schema_string<root_external_variant, non_hr_config>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("anyOf":[)"
                      R"({"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"({"type":"string"}]})");
}

ZEST_CASE(untagged_overlap_validates_as_any_of) {
    // A numeric enum's underlying range overlaps the int alternative: both
    // branches match the same document, so exactly-one (oneOf) semantics
    // would reject every value the encoder emits — anyOf must apply.
    using overlapping = std::variant<color_i8, std::int32_t>;
    const auto result = json::schema_string<overlapping>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("anyOf":[)"
                      R"({"type":"integer","minimum":-128,"maximum":127},)"
                      R"({"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}]})");
}

struct camel_deny_config {
    using field_rename = naming::rename_policy::lower_camel;
    [[maybe_unused]] constexpr static bool deny_unknown_fields = true;
};

ZEST_CASE(schema_agrees_with_encoder_on_config) {
    // The schema must accept what to_string under the same config emits:
    // renamed field names and the unknown-field policy.
    const auto encoded = json::to_string<camel_deny_config>(casing_child{.first_value = 7});
    ZASSERT(encoded);
    ZEXPECT(*encoded == R"({"firstValue":7})");

    const auto result = json::schema_string<casing_child, camel_deny_config>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("firstValue":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["firstValue"],)"
                      R"("additionalProperties":false})");
}

// meta::repr: the schema describes the document the encoder writes, which the
// codec kit's repr area pins.
ZEST_CASE(schema_follows_repr) {
    auto schema = json::schema_string<test::Symbol>();
    ZASSERT(schema);
    ZEXPECT(zest::contains(*schema, R"("rel":{"type":"integer")"));
    ZEXPECT(zest::contains(*schema, R"("ver":{"type":"string"})"));
    ZEXPECT(!zest::contains(*schema, "enum"));
}

ZEST_CASE(schema_of_dynamic_repr_is_any) {
    auto schema = json::schema_string<test::DynamicPair>();
    ZASSERT(schema);
    ZEXPECT(zest::contains(*schema, R"("number":{})"));
}

ZEST_CASE(schema_follows_chained_repr) {
    auto schema = json::schema_string<test::Field<test::Ticket>>();
    ZASSERT(schema);
    ZEXPECT(zest::contains(*schema, R"("value":{"type":"integer")"));
}

ZEST_CASE(schema_follows_annotation_in_repr_type) {
    auto schema = json::schema_string<test::Field<test::BasisPoints>>();
    ZASSERT(schema);
    ZEXPECT(zest::contains(*schema, R"("value":{"anyOf":[{"type":"number"},{"type":"null"}]})"));
}

ZEST_CASE(schema_follows_struct_attrs_in_repr_type) {
    auto schema = json::schema_string<test::Field<test::LineRange>>();
    ZASSERT(schema);
    ZEXPECT(zest::contains(*schema, R"("startLine")"));
    ZEXPECT(!zest::contains(*schema, R"("start_line")"));
    ZEXPECT(zest::contains(*schema, R"("additionalProperties":false)"));
}

ZEST_CASE(schema_follows_tagging_in_repr_type) {
    auto schema = json::schema_string<test::LoadResult>();
    ZASSERT(schema);
    ZEXPECT(zest::contains(*schema, R"("status":{"const":"err"})"));
}

ZEST_CASE(schema_of_tagged_variant_ignores_its_types_repr) {
    // The tag wins over the variant type's repr, as in the encoder.
    auto schema = json::schema_string<test::Field<test::TaggedStampOrNote>>();
    ZASSERT(schema);
    ZEXPECT(zest::contains(*schema, R"("t":{"const":"stamp"})"));
    ZEXPECT(zest::contains(*schema, R"("t":{"const":"note"})"));
}

ZEST_CASE(schema_follows_outer_policy_on_repr_alternatives) {
    auto schema =
        json::schema_string<test::Field<annotate<test::StrictCamelTag>::type<test::LoadResult>>>();
    ZASSERT(schema);
    ZEXPECT(zest::contains(*schema, R"("byteCount")"));
    ZEXPECT(!zest::contains(*schema, R"("byte_count")"));
}

ZEST_CASE(schema_keeps_nullable_repr_field_required) {
    auto schema = json::schema_string<test::Field<test::Lamport>>();
    ZASSERT(schema);
    ZEXPECT(zest::contains(*schema, R"("required":["value"])"));
}

ZEST_CASE(schema_follows_json_scoped_repr) {
    ZEXPECT(zest::type_eq<resolved_repr_t<test::Journal>, std::string>());
    ZEXPECT(zest::type_eq<resolved_repr_t<test::Journal, json::format>, std::int64_t>());
    auto schema = json::schema_string<test::Field<test::Journal>>();
    ZASSERT(schema);
    ZEXPECT(zest::contains(*schema, R"("value":{"type":"integer")"));
}

};  // ZEST_SUITE(codec_json_schema_encoder)

}  // namespace

}  // namespace kota::meta
