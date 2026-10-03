#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/containers.h"
#include "codec/harness/fixtures/structs.h"
#include "codec/harness/fixtures/tagged.h"
#include "fixtures/attrs.h"
#include "fixtures/configs.h"
#include "kota/zest/zest.h"
#include "kota/meta/compare.h"
#include "kota/codec/dyn/dyn.h"
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

ZEST_CASE(value_overload_value_initializes) {
    // `T value{}` would copy-list-initialize the explicit list from `{}`.
    auto result = json::from_string<test::HoldsExplicit>(R"({"list":[1,2],"count":2})");
    ASSERT(result);
    const test::HoldsExplicit expected{
        .list = {1, 2},
        .count = 2
    };
    EXPECT(meta::eq(*result, expected));
}

ZEST_CASE(number_out_of_range_fails) {
    std::uint8_t out = 0;
    auto status = json::from_string("300", out);
    ASSERT(!status);
    EXPECT(status.error().message == "number out of range");
}

ZEST_CASE(type_mismatch_fails) {
    // A leaf's type error is simdjson's own message.
    bool flag = false;
    auto status = json::from_string("1", flag);
    ASSERT(!status);
    EXPECT(status.error().message == incorrect_type);
}

ZEST_CASE(malformed_document_fails) {
    std::vector<int> out;
    auto status = json::from_string("[1 2]", out);
    ASSERT(!status);
    EXPECT(zest::starts_with(status.error().message, "TAPE_ERROR"));
}

ZEST_CASE(empty_document_fails) {
    // Rejected before any reading starts, so there is no location.
    std::vector<int> out;
    auto status = json::from_string("", out);
    ASSERT(!status);
    EXPECT(status.error().message == "EMPTY: no JSON found");
    EXPECT(!status.error().location);
}

ZEST_CASE(char_reads_one_codepoint_up_to_255) {
    // Two bytes of UTF-8 under either lead byte: C2 for U+0080-U+00BF, C3
    // above.
    char out = '\0';
    ASSERT(json::from_string(R"("\u0080")", out));
    EXPECT(out == static_cast<char>(0x80));
    ASSERT(json::from_string(R"("§")", out));
    EXPECT(out == static_cast<char>(0xA7));
    ASSERT(json::from_string(R"("é")", out));
    EXPECT(out == static_cast<char>(0xE9));
    ASSERT(json::from_string(R"("ÿ")", out));
    EXPECT(out == static_cast<char>(0xFF));
}

ZEST_CASE(char_beyond_255_fails) {
    char out = '\0';
    auto status = json::from_string(R"("Ā")", out);
    ASSERT(!status);
    EXPECT(status.error().message == codec::invalid_char_message);
    auto three_bytes = json::from_string(R"("€")", out);
    ASSERT(!three_bytes);
    EXPECT(three_bytes.error().message == codec::invalid_char_message);
}

ZEST_CASE(char_with_a_bad_continuation_byte_fails) {
    // simdjson checks the document is UTF-8 before anything reads it.
    char out = '\0';
    auto status = json::from_string("\"\xC3\x28\"", out);
    ASSERT(!status);
    EXPECT(status.error().message == "UTF8_ERROR: The input is not valid UTF-8");
}

ZEST_CASE(char_from_several_characters_fails) {
    char out = '\0';
    auto status = json::from_string(R"("xy")", out);
    ASSERT(!status);
    EXPECT(status.error().message == codec::invalid_char_message);
}

ZEST_CASE(byte_out_of_range_fails) {
    std::vector<std::byte> out;
    auto status = json::from_string("[0,256]", out);
    ASSERT(!status);
    EXPECT(status.error().message == "byte value out of range");
}

ZEST_CASE(scalar_root_trailing_content_fails) {
    int out = 0;
    auto status = json::from_string("1 2", out);
    ASSERT(!status);
    EXPECT(zest::starts_with(status.error().message, "TRAILING_CONTENT"));
}

ZEST_CASE(object_root_trailing_value_fails) {
    // The document is checked before it is decoded, so `out` keeps its value.
    test::Point out{.x = 5, .y = 6};
    auto status = json::from_string(R"({"x":1,"y":2} {"x":3})", out);
    ASSERT(!status);
    EXPECT(zest::starts_with(status.error().message, "TRAILING_CONTENT"));
    EXPECT(out.x == 5);
    EXPECT(out.y == 6);
}

ZEST_CASE(array_root_trailing_content_fails) {
    std::vector<int> list;
    auto status = json::from_string("[1] ]", list);
    ASSERT(!status);
    EXPECT(zest::starts_with(status.error().message, "TRAILING_CONTENT"));
}

ZEST_CASE(root_followed_by_whitespace_reads) {
    std::vector<int> list;
    ASSERT(json::from_string("[1, 2] \n\t ", list));
    EXPECT(list == std::vector<int>{1, 2});
}

ZEST_CASE(integer_beyond_64_bits_reads_as_double) {
    auto parsed = json::from_string<dyn::Value>(R"([18446744073709551616,-9223372036854775809])");
    ASSERT(parsed);
    EXPECT((*parsed)[0].get_double() == 18446744073709551616.0);
    EXPECT((*parsed)[1].get_double() == -9223372036854775809.0);
}

ZEST_CASE(dyn_value_reads) {
    // Any document reads into a dyn::Value, which writes the same document
    // and reads on into the typed value.
    test::PersonWithScores typed{
        .id = 7,
        .name = "alice",
        .scores = {10, 20},
        .active = true
    };
    auto text = json::to_string(typed);
    ASSERT(text);
    auto tree = json::from_string<dyn::Value>(*text);
    ASSERT(tree);
    EXPECT(json::to_string(*tree) == *text);
    auto again = dyn::from_dyn<test::PersonWithScores>(*tree);
    ASSERT(again);
    EXPECT(meta::eq(*again, typed));

    auto nested = json::from_string<dyn::Value>(R"([1,"two",true,null,[3,{"k":[]}]])");
    ASSERT(nested);
    EXPECT(json::to_string(*nested) == R"([1,"two",true,null,[3,{"k":[]}]])");
}

ZEST_CASE(dyn_value_takes_each_kind) {
    // An integer lands on signed_int, or on unsigned_int above int64's
    // maximum; a number with a point or beyond 64 bits on floating.
    auto tree = json::from_string<dyn::Value>(
        R"([null,true,42,-1,18446744073709551615,18446744073709551616,1.0,"x",[],{}])");
    ASSERT(tree);
    ASSERT(tree->is_array());
    std::vector<dyn::ValueKind> kinds;
    for(const auto& value: tree->as_array()) {
        kinds.push_back(value.kind());
    }
    using enum dyn::ValueKind;
    EXPECT(kinds == std::vector{null_value,
                                boolean,
                                signed_int,
                                signed_int,
                                unsigned_int,
                                floating,
                                floating,
                                string,
                                array,
                                object});
}

ZEST_CASE(deep_document_roundtrip) {
    // Sixteen levels, objects and arrays in turn, read into a tree and
    // written back as they were.
    constexpr int depth = 16;
    std::string open;
    std::string close;
    for(int level = 0; level < depth; ++level) {
        open += level % 2 == 0 ? R"({"k":)" : "[";
        close.insert(0, level % 2 == 0 ? "}" : "]");
    }
    auto text = open + "1" + close;
    auto tree = json::from_string<dyn::Value>(text);
    ASSERT(tree);
    auto cursor = tree->cursor();
    for(int level = 0; level < depth; ++level) {
        cursor = level % 2 == 0 ? cursor["k"] : cursor[0];
    }
    EXPECT(cursor.get_int() == std::int64_t{1});
    EXPECT(json::to_string(*tree) == text);
}

ZEST_CASE(located_type_mismatch_fails) {
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

ZEST_CASE(nested_type_mismatch_text_fails) {
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

ZEST_CASE(internal_duplicate_tag_fails) {
    test::InternalShape out;
    auto status = json::from_string(R"({"kind":"circle","kind":"rect","radius":1})", out);
    ASSERT(!status);
    EXPECT(status.error().message == "internally tagged variant: duplicate tag field");
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
