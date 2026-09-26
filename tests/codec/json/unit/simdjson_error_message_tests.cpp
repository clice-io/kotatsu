#include <cstdint>
#include <string>
#include <vector>

#include "fixtures/schema/common.h"
#include "kota/zest/zest.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

using namespace meta;

namespace {

using json::from_string;

using person = meta::fixtures::Person;
using with_scores = meta::fixtures::WithScores;

KOTATSU_ANNOTATION(strict_payload_annotation, deny_unknown_fields = true);
using strict_payload = annotate<strict_payload_annotation>::type<meta::fixtures::StrictIdName>;

enum class color { red, green, blue };

struct color_enum_string_tag {
    constexpr static auto spec =
        make_spec(dsl::enum_string = dsl::type<rename_policy::lower_camel>);
};

using color_enum_string = annotate<color_enum_string_tag>::type<color>;

ZEST_SUITE(codec_json_simdjson_error_message) {

ZEST_CASE(missing_required_field) {
    person parsed{};
    auto status = from_string(R"({"age": 25, "addr": {"city": "NY", "zip": 10001}})", parsed);
    EXPECT(!status);
    EXPECT(status.error().message == "missing required field 'name'");
}

ZEST_CASE(unknown_field_denied) {
    strict_payload parsed{};
    auto status = from_string(R"({"id": 1, "name": "ok", "extra": true})", parsed);
    EXPECT(!status);
    EXPECT(status.error().message == "unknown field 'extra'");
}

ZEST_CASE(nested_field_error_path) {
    person parsed{};
    auto status =
        from_string(R"({"name": "alice", "age": 30, "addr": {"city": "NY", "zip": "wrong"}})",
                    parsed);
    EXPECT(!status);
    EXPECT((status.error().message.find("type") != std::string::npos ||
            status.error().message.find("invalid") != std::string::npos));
    EXPECT(status.error().format_path() == "addr.zip");
}

ZEST_CASE(sequence_element_error_path) {
    std::vector<int> parsed;
    auto status = from_string(R"([1, 2, "bad", 4])", parsed);
    EXPECT(!status);
    EXPECT((status.error().message.find("type") != std::string::npos ||
            status.error().message.find("invalid") != std::string::npos));
    EXPECT(status.error().format_path() == "[2]");
}

ZEST_CASE(nested_sequence_error_path) {
    with_scores parsed{};
    auto status = from_string(R"({"name": "bob", "scores": [10, "bad", 30]})", parsed);
    EXPECT(!status);
    EXPECT((status.error().message.find("type") != std::string::npos ||
            status.error().message.find("invalid") != std::string::npos));
    EXPECT(status.error().format_path() == "scores[1]");
}

ZEST_CASE(enum_string_error_message) {
    color_enum_string parsed = color::red;
    auto status = from_string(R"("yellow")", parsed);
    EXPECT(!status);
    EXPECT(zest::contains(status.error().message, "yellow"));
}

ZEST_CASE(number_out_of_range) {
    std::uint8_t parsed = 0;
    auto status = from_string("300", parsed);
    EXPECT(!status);
    EXPECT((status.error().message.find("range") != std::string::npos ||
            status.error().message.find("out of") != std::string::npos));
}

ZEST_CASE(error_has_location) {
    person parsed{};
    auto status = from_string(R"({
  "name": "alice",
  "age": "not_a_number"
})",
                              parsed);
    EXPECT(!status);
    EXPECT((status.error().message.find("type") != std::string::npos ||
            status.error().message.find("invalid") != std::string::npos));
    EXPECT(status.error().format_path() == "age");
    EXPECT(status.error().location);
    EXPECT(status.error().location->line == 3u);
    EXPECT(status.error().location->column == 10u);
    EXPECT(status.error().location->byte_offset == 30u);
}

ZEST_CASE(to_string_combines_all) {
    person parsed{};
    auto status =
        from_string(R"({"name": "alice", "age": 30, "addr": {"city": "NY", "zip": "wrong"}})",
                    parsed);
    EXPECT(!status);
    auto str = status.error().to_string();
    EXPECT(zest::contains(str, "addr.zip"));
    EXPECT(zest::contains(str, "line 1"));
    EXPECT(zest::contains(str, "column 60"));
}

};  // ZEST_SUITE(codec_json_simdjson_error_message)

}  // namespace

}  // namespace kota::codec
