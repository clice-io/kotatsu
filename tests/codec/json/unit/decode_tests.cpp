#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "fixtures/attrs.h"
#include "fixtures/configs.h"
#include "fixtures/structs.h"
#include "fixtures/tagged.h"
#include "kota/zest/zest.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

namespace {

constexpr std::string_view incorrect_type =
    "INCORRECT_TYPE: The JSON element does not have the requested type.";

ZEST_SUITE(codec_json_decode) {

ZEST_CASE(value_overload_takes_config) {
    auto result = json::from_string<test::RenameAllTarget, test::CamelConfig>(
        R"({"userName":2,"totalScore":1.5,"itemId":"abc"})");
    ASSERT(result);
    EXPECT(result->user_name == 2);
    EXPECT(result->total_score == 1.5F);
    EXPECT(result->item_id == "abc");
}

ZEST_CASE(number_out_of_range_fails) {
    std::uint8_t out = 0;
    auto status = json::from_string("300", out);
    ASSERT(!status);
    EXPECT(status.error().message == "number out of range");
}

ZEST_CASE(type_mismatch_has_location) {
    test::Person out{};
    auto status = json::from_string(R"({
  "name": "alice",
  "age": "not_a_number"
})",
                                    out);
    ASSERT(!status);
    EXPECT(status.error().message == incorrect_type);
    EXPECT(status.error().format_path() == "age");
    ASSERT(status.error().location);
    EXPECT(status.error().location->line == 3U);
    EXPECT(status.error().location->column == 10U);
    EXPECT(status.error().location->byte_offset == 30U);
}

ZEST_CASE(error_text_has_path_and_location) {
    test::Person out{};
    auto status =
        json::from_string(R"({"name": "alice", "age": 30, "addr": {"city": "NY", "zip": "wrong"}})",
                          out);
    ASSERT(!status);
    EXPECT(status.error().to_string() ==
           std::string(incorrect_type) + " at addr.zip (line 1, column 60)");
}

ZEST_CASE(raw_value_alternative_takes_any_kind) {
    // RawValue decodes through a dispatch override that accepts any value, so
    // probing never judges it by its declared shape.
    std::variant<int, RawValue, bool> out;
    ASSERT(json::from_string(R"("text")", out));
    ASSERT(out.index() == 1U);
    EXPECT(std::get<RawValue>(out).data == R"("text")");

    ASSERT(json::from_string("7", out));
    EXPECT(out.index() == 0U);
}

ZEST_CASE(optional_raw_value_alternative_takes_any_kind) {
    std::variant<std::optional<RawValue>, int> out;
    ASSERT(json::from_string(R"("text")", out));
    ASSERT(out.index() == 0U);
    const auto& raw = std::get<0>(out);
    ASSERT(raw);
    EXPECT(raw->data == R"("text")");

    ASSERT(json::from_string("null", out));
    ASSERT(out.index() == 0U);
    EXPECT(!std::get<0>(out));
}

ZEST_CASE(adjacent_duplicate_tag_fails) {
    test::AdjacentShape out;
    auto status = json::from_string(R"({"t":"number","t":"point","c":42})", out);
    ASSERT(!status);
    EXPECT(status.error().message == "adjacently tagged variant: duplicate tag field");
}

ZEST_CASE(adjacent_duplicate_content_fails) {
    test::AdjacentShape out;
    auto status = json::from_string(R"({"t":"number","c":1,"c":2})", out);
    ASSERT(!status);
    EXPECT(status.error().message == "adjacently tagged variant: duplicate content field");
}

};  // ZEST_SUITE(codec_json_decode)

}  // namespace

}  // namespace kota::codec
