#if __has_include(<toml++/toml.hpp>)

#include <map>
#include <string>
#include <vector>

#include "fixtures/schema/common.h"
#include "kota/zest/zest.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/codec/toml/toml.h"

namespace kota::codec {

using namespace meta;

namespace {

using toml::from_string;
using toml::from_string;

using person = meta::fixtures::Person;
using with_scores = meta::fixtures::WithScores;

KOTATSU_ANNOTATION(strict_payload_annotation, deny_unknown_fields = true);
using strict_payload = annotate<strict_payload_annotation>::type<meta::fixtures::StrictIdName>;

enum class color { red, green, blue };

struct enum_string_color_tag {
    constexpr static auto spec =
        make_spec(dsl::enum_string = dsl::type<rename_policy::lower_camel>);
};

using enum_string_color = annotate<enum_string_color_tag>::type<color>;

ZEST_SUITE(serde_toml_error_message){

    ZEST_CASE(map_key_parse_error){auto result = from_string<std::map<int, int>>("abc = 1");
ASSERT(!result.has_value());
EXPECT(zest::contains(result.error().message, "cannot parse map key 'abc'"));

}  // namespace

ZEST_CASE(missing_required_field) {
    auto result = from_string<person>(R"(
age = 25
[addr]
city = "NY"
zip = 10001
)");
    ASSERT(!result.has_value());
    auto& e = result.error();
    EXPECT(e.message == "missing required field 'name'");
}

ZEST_CASE(unknown_field_denied) {
    auto result = from_string<strict_payload>(R"(
id = 1
name = "ok"
extra = true
)");
    ASSERT(!result.has_value());
    auto& e = result.error();
    EXPECT(e.message == "unknown field 'extra'");
    // The TOML backend attaches the offending value node's location.
    ASSERT(e.location.has_value());
    EXPECT(e.location->line == 4);
    EXPECT(e.location->column == 9);
}

ZEST_CASE(syntax_error_has_location) {
    // Unterminated string — a tokenizer-level error, reported by parse_table
    // through toml++'s parse_result (toml++ is pinned to TOML_EXCEPTIONS=0,
    // see kota/codec/toml/type.h).
    auto result = from_string<person>(R"(
name = "alice
age = 30
)");
    ASSERT(!result.has_value());
    auto& e = result.error();
    EXPECT(zest::starts_with(e.message, "TOML parse error: "));
    EXPECT(e.message.size() > std::string_view("TOML parse error: ").size());
    ASSERT(e.location.has_value());
    EXPECT(e.location->line == 2);
}

ZEST_CASE(nested_field_error_path) {
    auto result = from_string<person>(R"(
name = "alice"
age = 30
[addr]
city = "NY"
zip = "wrong"
)");
    ASSERT(!result.has_value());
    auto& e = result.error();
    EXPECT(e.format_path() == "addr.zip");
    EXPECT((e.message.find("invalid type") != std::string::npos ||
            e.message.find("type") != std::string::npos));
}

ZEST_CASE(sequence_element_error_path) {
    // TOML does not allow mixed-type arrays, so we use a struct with a vector
    // field and provide a table array where an element has the wrong type.
    // Instead, we use a TOML array with string elements for an int vector field.
    auto result = from_string<with_scores>(R"(
name = "bob"
scores = ["bad"]
)");
    ASSERT(!result.has_value());
    auto& e = result.error();
    EXPECT(e.format_path() == "scores[0]");
}

ZEST_CASE(enum_string_error_message) {
    enum_string_color parsed = color::red;
    auto table = toml::parse_table(R"(__value = "purple")");
    ASSERT(table.has_value());
    auto status = toml::from_toml(*table, parsed);
    ASSERT(!status.has_value());
    auto& e = status.error();
    EXPECT(zest::contains(e.message, "purple"));
}

ZEST_CASE(error_has_location) {
    auto result = from_string<person>(R"(
name = "alice"
age = "not_a_number"
)");
    ASSERT(!result.has_value());
    auto& e = result.error();
    EXPECT(e.location.has_value());
    // "age" is on line 3 (line 1 is empty after the raw string opening)
    EXPECT(e.location->line == 3);
    EXPECT(e.location->column == 7);
}

ZEST_CASE(to_string_combines_all) {
    auto result = from_string<person>(R"(
name = "alice"
age = 30
[addr]
city = "NY"
zip = "wrong"
)");
    ASSERT(!result.has_value());
    auto& e = result.error();
    // to_string format: "message at path (line L, column C)"
    auto str = e.to_string();
    EXPECT(zest::contains(str, "addr.zip"));
    EXPECT(zest::contains(str, "line 6"));
    EXPECT(zest::contains(str, "column 7"));
}

};  // namespace kota::codec

}  // namespace

}  // namespace kota::codec

#endif
