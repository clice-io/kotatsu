#include <cstdint>
#include <limits>
#include <string>
#include <variant>

#include "codec/harness/fixtures/repr.h"
#include "codec/harness/fixtures/structs.h"
#include "codec/json/harness/schema.h"
#include "fixtures/repr.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/codec/attribute.h"
#include "kotatsu/codec/json/schema.h"

namespace kotatsu::codec {

namespace {

using test::color_i8;
using test::with_enum;
using test::casing_child;
using test::root_external_variant;

namespace json = kotatsu::codec::json;

ZEST_SUITE(codec_json_schema_encoder) {

ZEST_CASE(enum_names_list_the_names) {
    const auto result = json::schema_string<test::named_color>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("enum":["red","green","blue"]})");
}

ZEST_CASE(unnamed_enum_value_rejected_by_enum_names) {
    // A representable value without a reflected member name has no string
    // spelling: the encoder rejects it, so the schema's enum list of
    // reflected names stays exhaustive.
    const auto encoded = json::to_string(static_cast<test::named_color>(42));
    ZEXPECT(!encoded);
}

ZEST_CASE(schema_agrees_with_encoder_on_enums) {
    // Under the default format the encoder emits the numeric value, so the
    // schema constrains the same numeric form.
    const auto encoded = json::to_string(with_enum{.c = color_i8::green, .name = "g"});
    ZASSERT(encoded);
    ZEXPECT(zest::contains(*encoded, R"("c":1)"));

    const auto schema = json::schema_string<with_enum>().value();
    ZEXPECT(zest::contains(schema, R"("c":{"type":"integer","minimum":-128,"maximum":127})"));
}

ZEST_CASE(schema_agrees_with_encoder_on_enum_rename) {
    // The schema lists the spellings the encoder writes, not the raw
    // reflected names.
    const auto encoded = json::to_string(test::shouting_color::green);
    ZASSERT(encoded);
    ZEXPECT(*encoded == R"("GREEN")");

    const auto result = json::schema_string<test::shouting_color>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("enum":["RED","GREEN","BLUE"]})");
}

ZEST_CASE(schema_agrees_with_encoder_on_non_finite_floats) {
    // JSON has no number for NaN or an infinity: the encoder fails on one,
    // and the schema admits only numbers.
    ZEXPECT(!json::to_string(std::numeric_limits<double>::quiet_NaN()).has_value());
    ZEXPECT(!json::to_string(std::numeric_limits<float>::infinity()).has_value());

    const auto result = json::schema_string<double>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"number"})");
}

ZEST_CASE(long_double_beyond_double_fails_as_infinity) {
    // A finite long double beyond double's range narrows to an infinity in
    // the document, which the encoder rejects as it does any.
    constexpr long double big = std::numeric_limits<long double>::max();
    if constexpr(big > static_cast<long double>(std::numeric_limits<double>::max())) {
        ZEXPECT(!json::to_string(big).has_value());
        ZEXPECT(!json::to_string(-big).has_value());
    } else {
        // long double is double: the value stays a finite number.
        ZEXPECT(json::to_string(big).has_value());
    }
}

struct non_hr_format : codec::extend<json::format> {
    constexpr static bool human_readable = false;
};

ZEST_CASE(schema_agrees_with_encoder_on_non_human_readable) {
    // A format that is not human readable bypasses tagging and encodes the
    // underlying variant, so the schema describes the untagged alternatives.
    const auto encoded = json::to_string<non_hr_format>(root_external_variant{7});
    ZASSERT(encoded);
    ZEXPECT(*encoded == "7");

    const auto result = json::schema_string<root_external_variant, non_hr_format>().value();
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

struct camel_format : codec::extend<json::format> {
    using rename_all = naming::rename_policy::lower_camel;
};

ZEST_CASE(schema_agrees_with_encoder_on_format) {
    // The schema must accept what to_string under the same format emits:
    // renamed field names; and deny the unknown fields a decode denies.
    const auto encoded = json::to_string<camel_format>(casing_child{.first_value = 7});
    ZASSERT(encoded);
    ZEXPECT(*encoded == R"({"firstValue":7})");

    const auto result =
        json::schema_string<casing_child, camel_format>({.deny_unknown_fields = true}).value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("firstValue":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["firstValue"],)"
                      R"("additionalProperties":false})");
}

// codec::repr: the schema describes the document the encoder writes, which the
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
    auto schema = json::schema_string<test::StrictLoad>();
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

}  // namespace kotatsu::codec
