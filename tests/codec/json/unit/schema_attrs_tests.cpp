#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/configs.h"
#include "codec/json/harness/schema.h"
#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/codec/json/schema.h"
#include "kota/codec/macro.h"

namespace kota::meta {

namespace {

using test::point2d;
using test::casing_child;

// ---------------------------------------------------------------------------
// Attributes
// ---------------------------------------------------------------------------
struct with_default {
    std::string name;
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> count;
};

struct with_skip {
    std::string visible;
    KOTATSU_ANNOTATE(skip = true)
    <std::int32_t> hidden;
};

struct base_fields {
    std::int32_t a;
    std::int32_t b;
};

struct with_flatten {
    KOTATSU_ANNOTATE(flatten = true)
    <base_fields> base;
    std::string extra;
};

struct with_rename {
    KOTATSU_ANNOTATE(rename = "my_field")
    <std::int32_t> x;
    std::string y;
};

struct with_skip_when {
    std::string name;
    KOTATSU_ANNOTATE(skip_if = skip_when::empty)
    <std::vector<std::int32_t>> tags;
    KOTATSU_ANNOTATE(skip_if = skip_when::default_value)
    <std::int32_t> count;
    KOTATSU_ANNOTATE(skip_if = type<pred::empty>)
    <std::string> note;
};

struct repeated_child_annotation {
    KOTATSU_ANNOTATE(rename_all = Casing::LowerCamel)
    <casing_child> left;
    KOTATSU_ANNOTATE(rename_all = Casing::LowerCamel)
    <casing_child> right;
};

// ---------------------------------------------------------------------------
// Additional types
// ---------------------------------------------------------------------------
struct all_default {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> x;
    KOTATSU_ANNOTATE(defaulted = true)
    <std::string> y;
};

struct skip_default {
    std::string name;
    KOTATSU_ANNOTATE(skip = true)
    <std::int32_t> hidden;
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> count;
};

struct base_with_opt {
    std::int32_t x;
    std::optional<std::int32_t> y;
};

struct flatten_opt {
    KOTATSU_ANNOTATE(flatten = true)
    <base_with_opt> base;
    std::string tag;
};

struct rename_base {
    KOTATSU_ANNOTATE(rename = "alpha")
    <std::int32_t> a;
    std::int32_t b;
};

struct flatten_rename {
    KOTATSU_ANNOTATE(flatten = true)
    <rename_base> inner;
    std::string extra;
};

// ---------------------------------------------------------------------------
// fixtures of what an annotation states for a schema alone
// ---------------------------------------------------------------------------
struct bounded_fields {
    KOTATSU_ANNOTATE(minimum = 1, maximum = 64)
    <std::uint32_t> workers;
    /// Looser than its type, whose bound stays.
    KOTATSU_ANNOTATE(minimum = -1000)
    <std::int8_t> offset;
    KOTATSU_ANNOTATE(minimum = 0.5)
    <double> ratio;
    KOTATSU_ANNOTATE(maximum = 10)
    <std::optional<std::int32_t>> limit;
    /// Nullable twice over: its own null, and nan_repr's.
    KOTATSU_ANNOTATE(minimum = 0.25)
    <std::optional<double>> share;
};

struct bounded_text {
    KOTATSU_ANNOTATE(minimum = 1)
    <std::string> name;
};

struct fractional_bound_on_integer {
    KOTATSU_ANNOTATE(maximum = 2.5)
    <std::int32_t> count;
};

struct chosen_fields {
    KOTATSU_ANNOTATE(choices = {"off", "on", "auto"})
    <std::string> readonly;
    KOTATSU_ANNOTATE(choices = {"a", "b"})
    <std::optional<std::string>> mode;
};

struct chosen_number {
    KOTATSU_ANNOTATE(choices = {"1"})
    <std::int32_t> count;
};

struct machine_section {
    KOTATSU_ANNOTATE(defaulted = true, schema_default = false)
    <std::uint32_t> workers = 8;
    KOTATSU_ANNOTATE(defaulted = true)
    <std::uint32_t> retries = 3;
};

struct machine_root {
    KOTATSU_ANNOTATE(defaulted = true)
    <machine_section> section;
    KOTATSU_ANNOTATE(defaulted = true, schema_default = false)
    <std::uint32_t> jobs = 8;
};

enum class level_kind : std::uint8_t {
    low_level,
    high_level,
};

/// Sections in an array, which carries their encoded objects as its default.
struct machine_sections {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::vector<machine_section>> sections = {machine_section{}};
};

struct bounded_enum {
    KOTATSU_ANNOTATE(maximum = 1)
    <level_kind> level;
};

struct wide_bounds {
    KOTATSU_ANNOTATE(minimum = -1, maximum = ~0ULL)
    <std::uint64_t> count;
};

struct aliased_required {
    KOTATSU_ANNOTATE(alias = {"legacy"})
    <std::int32_t> value;
};

/// A required field whose default a schema would leave unstated.
struct unstated_required {
    KOTATSU_ANNOTATE(schema_default = false)
    <std::uint32_t> seed = 1;
};

/// Two fields answering to "dup", and an alias taking another field's name.
struct shared_alias {
    KOTATSU_ANNOTATE(alias = {"dup"})
    <std::int32_t> left;
    KOTATSU_ANNOTATE(alias = {"dup"})
    <std::int32_t> right;
};

struct alias_on_a_name {
    KOTATSU_ANNOTATE(alias = {"right"})
    <std::int32_t> left;
    std::int32_t right;
};

struct enum_string_field {
    KOTATSU_ANNOTATE(enum_string = type<naming::rename_policy::lower_camel>)
    <level_kind> level;
};

struct keyed_maps {
    std::map<std::int32_t, std::int32_t> by_id;
    std::map<std::uint8_t, std::int32_t> by_byte;
    std::map<level_kind, std::int32_t> by_level;
};

struct char_field {
    char letter;
};

// ---------------------------------------------------------------------------
// description fixtures
// ---------------------------------------------------------------------------
struct desc_scalar {
    KOTATSU_ANNOTATE(description = "Number of worker threads.")
    <std::int32_t> threads;
    std::string name;
};

struct desc_optional {
    KOTATSU_ANNOTATE(description = "Optional display label.")
    <std::optional<std::string>> label;
};

struct desc_struct_ref {
    KOTATSU_ANNOTATE(description = "Anchor position.")
    <point2d> anchor;
};

struct desc_base {
    KOTATSU_ANNOTATE(description = "Inherited counter.")
    <std::int32_t> count;
};

struct desc_flatten {
    KOTATSU_ANNOTATE(flatten = true)
    <desc_base> base;
    std::string tag;
};

struct desc_rename {
    KOTATSU_ANNOTATE(rename = "max_size", description = "Maximum size in bytes.")
    <std::int32_t> size;
};

struct desc_default {
    KOTATSU_ANNOTATE(defaulted = true, description = "Retry limit.")
    <std::int32_t> retries;
};

struct desc_shared_ref {
    point2d origin;
    KOTATSU_ANNOTATE(description = "Anchor position.")
    <point2d> anchor;
};

struct desc_tagged_circle {
    KOTATSU_ANNOTATE(description = "Radius in meters.")
    <double> radius;
};

struct desc_tagged_rect {
    double width;
    double height;
};

KOTATSU_ANNOTATION(desc_internal_annotation, tag = "kind", tag_names = {"circle", "rect"});
using desc_internal_variant =
    annotate<desc_internal_annotation>::type<std::variant<desc_tagged_circle, desc_tagged_rect>>;

namespace json = kota::codec::json;

ZEST_SUITE(codec_json_schema_attrs) {

// ---------------------------------------------------------------------------
// default_value attribute
// ---------------------------------------------------------------------------

ZEST_CASE(attr_default_value) {
    const auto result = json::schema_string<with_default>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("name":{"type":"string"},)"
                      R"("count":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647,"default":0}},)"
                      R"("required":["name"]})");
}

ZEST_CASE(all_default_fields) {
    const auto result = json::schema_string<all_default>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("x":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647,"default":0},)"
                      R"("y":{"type":"string","default":""}}})");
}

// ---------------------------------------------------------------------------
// deny_unknown_fields
// ---------------------------------------------------------------------------

ZEST_CASE(deny_unknown_struct) {
    const static field_info deny_fields[] = {
        {"name",  {}, 0, 0, type_info_of<std::string>,  false, false, false},
        {"count", {}, 0, 1, type_info_of<std::int32_t>, false, false, false},
    };
    const static struct_type_info deny_info = {
        {type_kind::structure, "deny_struct"},
        true, // deny_unknown
        false, // is_trivial_layout
        {deny_fields,          2            },
    };
    const auto result = json::schema_string(deny_info).value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("name":{"type":"string"},)"
                      R"("count":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["name","count"],)"
                      R"("additionalProperties":false})");
}

// ---------------------------------------------------------------------------
// skip
// ---------------------------------------------------------------------------

ZEST_CASE(attr_skip) {
    const auto result = json::schema_string<with_skip>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("visible":{"type":"string"}},)"
                      R"("required":["visible"]})");
}

ZEST_CASE(skip_and_default) {
    const auto result = json::schema_string<skip_default>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("name":{"type":"string"},)"
                      R"("count":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647,"default":0}},)"
                      R"("required":["name"]})");
}

// A field the encoder may omit (built-in skip_when or a custom skip_if
// predicate) is never required — the decoder accepts its absence.
ZEST_CASE(skip_if_fields_not_required) {
    const auto result = json::schema_string<with_skip_when>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("name":{"type":"string"},)"
                      R"("tags":{"type":"array",)"
                      R"("items":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("count":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"("note":{"type":"string"}},)"
                      R"("required":["name"]})");
}

// Two KOTATSU_ANNOTATE uses expand to distinct tags; identical untagged
// struct specs must still collapse to one type_info instance and $defs entry.
ZEST_CASE(repeated_inline_struct_annotation_shares_def) {
    const auto result = json::schema_string<repeated_child_annotation>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("left":{"$ref":"#/$defs/casing_child"},)"
                      R"("right":{"$ref":"#/$defs/casing_child"}},)"
                      R"("required":["left","right"],)"
                      R"("$defs":{)"
                      R"("casing_child":{"type":"object",)"
                      R"("properties":{)"
                      R"("firstValue":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["firstValue"]}}})");
}

// ---------------------------------------------------------------------------
// flatten
// ---------------------------------------------------------------------------

ZEST_CASE(attr_flatten) {
    const auto result = json::schema_string<with_flatten>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("a":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"("b":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"("extra":{"type":"string"}},)"
                      R"("required":["a","b","extra"]})");
}

ZEST_CASE(flatten_with_optional) {
    const auto result = json::schema_string<flatten_opt>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("x":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"("y":{"anyOf":[{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"({"type":"null"}],"default":null},)"
                      R"("tag":{"type":"string"}},)"
                      R"("required":["x","tag"]})");
}

ZEST_CASE(flatten_with_rename) {
    const auto result = json::schema_string<flatten_rename>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("alpha":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"("b":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"("extra":{"type":"string"}},)"
                      R"("required":["alpha","b","extra"]})");
}

// ---------------------------------------------------------------------------
// rename
// ---------------------------------------------------------------------------

ZEST_CASE(attr_rename) {
    const auto result = json::schema_string<with_rename>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("my_field":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"("y":{"type":"string"}},)"
                      R"("required":["my_field","y"]})");
}

// ---------------------------------------------------------------------------
// what an annotation states for a schema alone
// ---------------------------------------------------------------------------

ZEST_CASE(bounds_join_the_number_schema) {
    const auto result = json::schema_string<bounded_fields>().value();
    // Tighter than the type's, they replace its bounds.
    ZEXPECT(zest::contains(result, R"("workers":{"type":"integer","minimum":1,"maximum":64})"));
    // Looser, they leave them.
    ZEXPECT(zest::contains(result, R"("offset":{"type":"integer","minimum":-128,"maximum":127})"));
    // A float or a nullable takes them in its number branch.
    ZEXPECT(zest::contains(result, R"("ratio":{"anyOf":[{"type":"number","minimum":0.5},)"));
    ZEXPECT(zest::contains(result, R"("minimum":-2147483648,"maximum":10},{"type":"null"}])"));
    ZEXPECT(zest::contains(
        result,
        R"("share":{"anyOf":[{"anyOf":[{"type":"number","minimum":0.25},{"type":"null"}]},)"));
}

ZEST_CASE(bound_on_a_field_that_is_no_number_fails) {
    auto text = json::schema_string<bounded_text>();
    ZASSERT(!text);
    ZEXPECT(text.error().message == "minimum or maximum on field 'name', which is not a number");
    auto fractional = json::schema_string<fractional_bound_on_integer>();
    ZASSERT(!fractional);
    ZEXPECT(fractional.error().message == "floating-point maximum on integer field 'count'");
}

ZEST_CASE(choices_join_the_string_schema) {
    const auto result = json::schema_string<chosen_fields>().value();
    ZEXPECT(zest::contains(result, R"("readonly":{"type":"string","enum":["off","on","auto"]})"));
    ZEXPECT(
        zest::contains(result,
                       R"("mode":{"anyOf":[{"type":"string","enum":["a","b"]},{"type":"null"}])"));
}

ZEST_CASE(choices_on_a_field_that_is_no_string_fails) {
    auto result = json::schema_string<chosen_number>();
    ZASSERT(!result);
    ZEXPECT(result.error().message == "choices on field 'count', which is not a string");
}

ZEST_CASE(unstated_default_leaves_the_document_alone) {
    // Only the schema's default documents leave the field out.
    auto text = json::to_string(machine_root{});
    ZASSERT(text);
    ZEXPECT(*text == R"({"section":{"workers":8,"retries":3},"jobs":8})");
}

ZEST_CASE(unstated_default_leaves_element_defaults_out) {
    const auto result = json::schema_string<machine_sections>().value();
    ZEXPECT(zest::contains(result, R"("default":[{"retries":3}])"));
    ZEXPECT(!zest::contains(result, R"("workers":8)"));
}

ZEST_CASE(bounds_on_an_enum_join_its_integer) {
    const auto result = json::schema_string<bounded_enum>().value();
    ZEXPECT(zest::contains(result, R"("level":{"type":"integer","minimum":0,"maximum":1})"));
    // Spelled by name, it is not a number.
    auto named = json::schema_string<bounded_enum, test::EnumStringConfig>();
    ZASSERT(!named);
    ZEXPECT(named.error().message == "minimum or maximum on field 'level', which is not a number");
}

ZEST_CASE(bounds_looser_than_uint64_keep_its_own) {
    const auto result = json::schema_string<wide_bounds>().value();
    ZEXPECT(
        zest::contains(result,
                       R"("count":{"type":"integer","minimum":0,"maximum":18446744073709551615})"));
}

ZEST_CASE(unstated_default_appears_nowhere) {
    // Neither its own property, its $def, nor a whole-object default holding
    // it states the default.
    const auto result = json::schema_string<machine_root>().value();
    ZEXPECT(
        zest::contains(result, R"("jobs":{"type":"integer","minimum":0,"maximum":4294967295})"));
    ZEXPECT(zest::contains(result, R"("default":{"retries":3})"));
    ZEXPECT(
        zest::contains(result, R"("workers":{"type":"integer","minimum":0,"maximum":4294967295})"));
    ZEXPECT(!zest::contains(result, R"("default":8)"));
}

ZEST_CASE(unstated_default_on_a_required_field_fails) {
    // A default holding the field could not leave it out.
    auto result = json::schema_string<unstated_required>();
    ZASSERT(!result);
    ZEXPECT(result.error().message == "schema_default = false on field 'seed', which is required");
}

ZEST_CASE(a_name_two_fields_answer_to_fails) {
    // The decoder gives the key to the first field answering to it.
    auto shared = json::schema_string<shared_alias>();
    ZASSERT(!shared);
    ZEXPECT(shared.error().message == "field 'right' answers to 'dup', as another field does");
    auto taken = json::schema_string<alias_on_a_name>();
    ZASSERT(!taken);
    ZEXPECT(taken.error().message == "field 'right' answers to 'right', as another field does");
}

ZEST_CASE(alias_is_allowed_where_unknown_fields_are_denied) {
    const auto result = json::schema_string<aliased_required, test::StrictConfig>().value();
    ZEXPECT(zest::contains(result, R"("legacy":{"type":"integer")"));
    ZEXPECT(zest::contains(result, R"("additionalProperties":false)"));
}

ZEST_CASE(alias_is_a_property_and_satisfies_required) {
    const auto result = json::schema_string<aliased_required>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("value":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                      R"("legacy":{"type":"integer","minimum":-2147483648,"maximum":2147483647}},)"
                      R"("allOf":[{"anyOf":[{"required":["value"]},{"required":["legacy"]}]}]})");
}

ZEST_CASE(field_enum_string_lists_its_members) {
    const auto result = json::schema_string<enum_string_field>().value();
    ZEXPECT(zest::contains(result, R"("level":{"enum":["lowLevel","highLevel"]})"));
}

ZEST_CASE(map_keys_spell_what_they_decode_from) {
    const auto result = json::schema_string<keyed_maps>().value();
    ZEXPECT(zest::contains(
        result,
        R"("by_id":{"type":"object","additionalProperties":{"type":"integer","minimum":-2147483648,"maximum":2147483647},"propertyNames":{"pattern":"^-?[0-9]+$"}})"));
    ZEXPECT(zest::contains(
        result,
        R"("by_byte":{"type":"object","additionalProperties":{"type":"integer","minimum":-2147483648,"maximum":2147483647},"propertyNames":{"pattern":"^[0-9]+$"}})"));
    // An enum key spells its number, or its name under enum_repr::String.
    ZEXPECT(zest::contains(
        result,
        R"("by_level":{"type":"object","additionalProperties":{"type":"integer","minimum":-2147483648,"maximum":2147483647},"propertyNames":{"pattern":"^[0-9]+$"}})"));
    const auto named = json::schema_string<keyed_maps, test::EnumStringConfig>().value();
    ZEXPECT(zest::contains(named, R"("propertyNames":{"enum":["low_level","high_level"]})"));
}

ZEST_CASE(char_is_one_code_point_up_to_ff) {
    const auto result = json::schema_string<char_field>().value();
    ZEXPECT(
        zest::contains(result, R"("letter":{"type":"string","pattern":"^[\\u0000-\\u00FF]$"})"));
}

// ---------------------------------------------------------------------------
// description
// ---------------------------------------------------------------------------

ZEST_CASE(description_on_scalar_field) {
    const auto result = json::schema_string<desc_scalar>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("threads":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647,)"
                      R"("description":"Number of worker threads."},)"
                      R"("name":{"type":"string"}},)"
                      R"("required":["threads","name"]})");
}

ZEST_CASE(description_on_optional_field) {
    const auto result = json::schema_string<desc_optional>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("label":{"anyOf":[{"type":"string"},)"
                      R"({"type":"null"}],)"
                      R"("description":"Optional display label.","default":null}}})");
}

ZEST_CASE(description_on_struct_ref_field) {
    const auto result = json::schema_string<desc_struct_ref>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("anchor":{"$ref":"#/$defs/point2d",)"
                      R"("description":"Anchor position."}},)"
                      R"("required":["anchor"],)"
                      R"("$defs":{)"
                      R"("point2d":{"type":"object",)"
                      R"("properties":{)"
                      R"("x":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"("y":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["x","y"]}}})");
}

ZEST_CASE(description_through_flatten) {
    const auto result = json::schema_string<desc_flatten>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("count":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647,)"
                      R"("description":"Inherited counter."},)"
                      R"("tag":{"type":"string"}},)"
                      R"("required":["count","tag"]})");
}

ZEST_CASE(description_with_rename) {
    const auto result = json::schema_string<desc_rename>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("max_size":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647,)"
                      R"("description":"Maximum size in bytes."}},)"
                      R"("required":["max_size"]})");
}

ZEST_CASE(description_with_default_value) {
    const auto result = json::schema_string<desc_default>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("retries":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647,)"
                      R"("description":"Retry limit.","default":0}}})");
}

ZEST_CASE(described_and_bare_struct_share_def) {
    const auto result = json::schema_string<desc_shared_ref>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("origin":{"$ref":"#/$defs/point2d"},)"
                      R"("anchor":{"$ref":"#/$defs/point2d",)"
                      R"("description":"Anchor position."}},)"
                      R"("required":["origin","anchor"],)"
                      R"("$defs":{)"
                      R"("point2d":{"type":"object",)"
                      R"("properties":{)"
                      R"("x":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647},)"
                      R"("y":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}},)"
                      R"("required":["x","y"]}}})");
}

ZEST_CASE(description_in_internal_tagged_alternative) {
    const auto result = json::schema_string<desc_internal_variant>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("oneOf":[)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("radius":{"anyOf":[{"type":"number"},{"type":"null"}],)"
                      R"("description":"Radius in meters."},)"
                      R"("kind":{"const":"circle"}},)"
                      R"("required":["radius","kind"]},)"
                      R"({"type":"object",)"
                      R"("properties":{)"
                      R"("width":{"anyOf":[{"type":"number"},{"type":"null"}]},)"
                      R"("height":{"anyOf":[{"type":"number"},{"type":"null"}]},)"
                      R"("kind":{"const":"rect"}},)"
                      R"("required":["width","height","kind"]}]})");
}

};  // ZEST_SUITE(codec_json_schema_attrs)

}  // namespace

}  // namespace kota::meta
