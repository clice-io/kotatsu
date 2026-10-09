#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/configs.h"
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

/// Whether the value-returning overload takes T.
template <typename T>
concept decodes_by_value = requires(std::string_view text) { json::from_string<T>(text); };

ZEST_SUITE(codec_json_decode) {

ZEST_CASE(value_overload_value_initializes) {
    // `T value{}` would copy-list-initialize the explicit list from `{}`.
    auto result = json::from_string<test::HoldsExplicit>(R"({"list":[1,2],"count":2})");
    ZASSERT(result);
    const test::HoldsExplicit expected{
        .list = {1, 2},
        .count = 2
    };
    ZEXPECT(meta::eq(*result, expected));
    ZSTATIC_EXPECT(decodes_by_value<test::HoldsExplicit>);
    // A type with no default constructor has no value to decode into.
    ZSTATIC_EXPECT(!decodes_by_value<test::NoDefault>);
}

ZEST_CASE(value_overload_takes_config) {
    auto result = json::from_string<test::RenameAllTarget, test::CamelConfig>(
        R"({"userName":2,"totalScore":1.5,"itemId":"abc"})");
    ZASSERT(result);
    ZEXPECT(result->user_name == 2);
    ZEXPECT(result->total_score == 1.5F);
    ZEXPECT(result->item_id == "abc");
}

ZEST_CASE(number_out_of_range_fails) {
    std::uint8_t out = 0;
    auto status = json::from_string("300", out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "number out of range");
}

ZEST_CASE(type_mismatch_fails) {
    // A leaf's type error is simdjson's own message.
    bool flag = false;
    auto status = json::from_string("1", flag);
    ZASSERT(!status);
    ZEXPECT(status.error().message == incorrect_type);
}

ZEST_CASE(malformed_document_fails) {
    std::vector<int> out;
    auto status = json::from_string("[1 2]", out);
    ZASSERT(!status);
    ZEXPECT(zest::starts_with(status.error().message, "TAPE_ERROR"));
}

ZEST_CASE(member_that_cannot_be_read_fails) {
    test::Point out{};
    auto status = json::from_string(R"({"x":1 "y":2})", out);
    ZASSERT(!status);
    ZEXPECT(zest::starts_with(status.error().message, "TAPE_ERROR"));
}

ZEST_CASE(padded_text_decodes_whatever_its_padding_holds) {
    // What lies in the padding is not read as the text, whatever it is.
    std::string text = R"({"x":1,"y":2})";
    const auto size = text.size();
    text.append(simdjson::SIMDJSON_PADDING, '}');
    const json::padded_string_view padded(text.data(), size, text.size());
    test::Point out{};
    ZASSERT(json::from_padded_string(padded, out));
    ZEXPECT(out.x == 1);
    ZEXPECT(out.y == 2);
    auto value = json::from_padded_string<test::Point>(padded);
    ZASSERT(value);
    ZEXPECT(value->y == 2);
}

ZEST_CASE(padded_text_without_its_padding_fails) {
    std::string text = "[1,2]";
    std::vector<int> out;
    auto status =
        json::from_padded_string(json::padded_string_view(text.data(), text.size(), text.size()),
                                 out);
    ZASSERT(!status);
    ZEXPECT(status.error().message ==
            simdjson::error_message(simdjson::error_code::INSUFFICIENT_PADDING));
}

ZEST_CASE(padded_text_locates_a_failure) {
    std::string text = R"({
  "name": "alice",
  "age": "not_a_number"
})";
    const auto size = text.size();
    text.append(simdjson::SIMDJSON_PADDING, ' ');
    test::Person out{};
    auto status =
        json::from_padded_string(json::padded_string_view(text.data(), size, text.size()), out);
    ZASSERT(!status);
    ZASSERT(status.error().location);
    ZEXPECT(status.error().location->line == 3U);
    ZEXPECT(status.error().location->column == 10U);
}

ZEST_CASE(empty_document_fails) {
    // Rejected before any reading starts, so there is no location.
    std::vector<int> out;
    auto status = json::from_string("", out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "EMPTY: no JSON found");
    ZEXPECT(!status.error().location);
}

ZEST_CASE(char_reads_one_codepoint_up_to_255) {
    // Two bytes of UTF-8 under either lead byte: C2 for U+0080-U+00BF, C3
    // above.
    char out = '\0';
    ZASSERT(json::from_string(R"("\u0080")", out));
    ZEXPECT(out == static_cast<char>(0x80));
    ZASSERT(json::from_string(R"("§")", out));
    ZEXPECT(out == static_cast<char>(0xA7));
    ZASSERT(json::from_string(R"("é")", out));
    ZEXPECT(out == static_cast<char>(0xE9));
    ZASSERT(json::from_string(R"("ÿ")", out));
    ZEXPECT(out == static_cast<char>(0xFF));
}

ZEST_CASE(char_beyond_255_fails) {
    char out = '\0';
    auto status = json::from_string(R"("Ā")", out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == codec::invalid_char_message);
    auto three_bytes = json::from_string(R"("€")", out);
    ZASSERT(!three_bytes);
    ZEXPECT(three_bytes.error().message == codec::invalid_char_message);
}

ZEST_CASE(char_with_a_bad_continuation_byte_fails) {
    // simdjson checks the document is UTF-8 before anything reads it.
    char out = '\0';
    auto status = json::from_string("\"\xC3\x28\"", out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "UTF8_ERROR: The input is not valid UTF-8");
}

ZEST_CASE(char_from_several_characters_fails) {
    char out = '\0';
    auto status = json::from_string(R"("xy")", out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == codec::invalid_char_message);
}

// A vector's elements are decoded where they end up; the one that fails is
// taken back, those before it stay.
ZEST_CASE(sequence_element_that_fails_is_not_kept) {
    std::vector<test::Point> points;
    auto status = json::from_string(R"([{"x":1,"y":2},{"x":3,"y":"four"}])", points);
    ZASSERT(!status);
    ZEXPECT(points == std::vector<test::Point>{
                          {.x = 1, .y = 2}
    });
}

ZEST_CASE(byte_out_of_range_fails) {
    std::vector<std::byte> out;
    auto status = json::from_string("[0,256]", out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "byte value out of range");
}

ZEST_CASE(scalar_root_trailing_content_fails) {
    int out = 0;
    auto status = json::from_string("1 2", out);
    ZASSERT(!status);
    ZEXPECT(zest::starts_with(status.error().message, "TRAILING_CONTENT"));
}

ZEST_CASE(object_root_trailing_value_fails) {
    // The document is checked before it is decoded, so `out` keeps its value.
    test::Point out{.x = 5, .y = 6};
    auto status = json::from_string(R"({"x":1,"y":2} {"x":3})", out);
    ZASSERT(!status);
    ZEXPECT(zest::starts_with(status.error().message, "TRAILING_CONTENT"));
    ZEXPECT(out.x == 5);
    ZEXPECT(out.y == 6);
}

ZEST_CASE(array_root_trailing_content_fails) {
    std::vector<int> list;
    auto status = json::from_string("[1] ]", list);
    ZASSERT(!status);
    ZEXPECT(zest::starts_with(status.error().message, "TRAILING_CONTENT"));
}

ZEST_CASE(root_followed_by_whitespace_reads) {
    std::vector<int> list;
    ZASSERT(json::from_string("[1, 2] \n\t ", list));
    ZEXPECT(list == std::vector<int>{1, 2});
}

ZEST_CASE(integer_beyond_64_bits_reads_as_double) {
    auto parsed = json::from_string<dyn::Value>(R"([18446744073709551616,-9223372036854775809])");
    ZASSERT(parsed);
    ZEXPECT((*parsed)[0].get_double() == 18446744073709551616.0);
    ZEXPECT((*parsed)[1].get_double() == -9223372036854775809.0);
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
    ZASSERT(text);
    auto tree = json::from_string<dyn::Value>(*text);
    ZASSERT(tree);
    ZEXPECT(json::to_string(*tree) == *text);
    auto again = dyn::from_dyn<test::PersonWithScores>(*tree);
    ZASSERT(again);
    ZEXPECT(meta::eq(*again, typed));

    auto nested = json::from_string<dyn::Value>(R"([1,"two",true,null,[3,{"k":[]}]])");
    ZASSERT(nested);
    ZEXPECT(json::to_string(*nested) == R"([1,"two",true,null,[3,{"k":[]}]])");
}

ZEST_CASE(dyn_value_takes_each_kind) {
    // An integer lands on signed_int, or on unsigned_int above int64's
    // maximum; a number with a point or beyond 64 bits on floating.
    auto tree = json::from_string<dyn::Value>(
        R"([null,true,42,-1,18446744073709551615,18446744073709551616,1.0,"x",[],{}])");
    ZASSERT(tree);
    ZASSERT(tree->is_array());
    std::vector<dyn::ValueKind> kinds;
    for(const auto& value: tree->as_array()) {
        kinds.push_back(value.kind());
    }
    using enum dyn::ValueKind;
    ZEXPECT(kinds == std::vector{null_value,
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
    ZASSERT(tree);
    auto cursor = tree->cursor();
    for(int level = 0; level < depth; ++level) {
        cursor = level % 2 == 0 ? cursor["k"] : cursor[0];
    }
    ZEXPECT(cursor.get_int() == std::int64_t{1});
    ZEXPECT(json::to_string(*tree) == text);
}

ZEST_CASE(located_type_mismatch_fails) {
    test::Person out{};
    auto status = json::from_string(R"({
  "name": "alice",
  "age": "not_a_number"
})",
                                    out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == incorrect_type);
    ZEXPECT(status.error().format_path() == "age");
    ZASSERT(status.error().location);
    ZEXPECT(status.error().location->line == 3U);
    ZEXPECT(status.error().location->column == 10U);
    ZEXPECT(status.error().location->byte_offset == 30U);
}

ZEST_CASE(unknown_field_fails_at_its_key) {
    test::Point out{};
    auto status = json::from_string<test::StrictConfig>(R"({
  "x": 1,
  "extra": true,
  "y": 2
})",
                                                        out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "unknown field 'extra'");
    ZASSERT(status.error().location);
    ZEXPECT(status.error().location->line == 3U);
    ZEXPECT(status.error().location->column == 3U);
}

ZEST_CASE(unknown_fields_reported_at_their_keys) {
    UnknownFields unknown;
    scoped_context<UnknownFields> scope(unknown);
    test::Person out{};
    auto status = json::from_string(R"({
  "name": "alice",
  "nick": "al",
  "age": 30,
  "addr": {"city": "NY", "zip": 10001, "floor": 3}
})",
                                    out);
    ZASSERT(status);
    ZEXPECT(out.addr.zip == 10001);
    ZASSERT(unknown.entries.size() == 2U);
    ZEXPECT(unknown.entries[0].to_string() == "unknown field 'nick' (line 3, column 3)");
    ZEXPECT(unknown.entries[1].to_string() == "unknown field 'floor' at addr (line 5, column 40)");
}

ZEST_CASE(unknown_field_in_an_element_reported_at_its_key) {
    UnknownFields unknown;
    scoped_context<UnknownFields> scope(unknown);
    std::vector<test::Point> out;
    auto status = json::from_string(R"([
  {"x": 1, "y": 2},
  {"x": 3, "z": 0, "y": 4}
])",
                                    out);
    ZASSERT(status);
    ZASSERT(unknown.entries.size() == 1U);
    ZEXPECT(unknown.entries[0].to_string() == "unknown field 'z' at [1] (line 3, column 12)");
}

ZEST_CASE(unknown_fields_of_two_decodes_keep_their_locations) {
    // One collector over two documents: each decode counts the lines of what
    // it reported in its own text.
    UnknownFields unknown;
    scoped_context<UnknownFields> scope(unknown);
    test::Point first{};
    ZASSERT(json::from_string("{\n\n\"x\": 1, \"y\": 2, \"a\": 0}", first));
    test::Point second{};
    ZASSERT(json::from_string(R"({"b": 0, "x": 1, "y": 2})", second));
    ZASSERT(unknown.entries.size() == 2U);
    ZEXPECT(unknown.entries[0].to_string() == "unknown field 'a' (line 3, column 17)");
    ZEXPECT(unknown.entries[1].to_string() == "unknown field 'b' (line 1, column 2)");
}

ZEST_CASE(nested_type_mismatch_text_fails) {
    test::Person out{};
    auto status =
        json::from_string(R"({"name": "alice", "age": 30, "addr": {"city": "NY", "zip": "wrong"}})",
                          out);
    ZASSERT(!status);
    ZEXPECT(status.error().to_string() ==
            std::string(incorrect_type) + " at addr.zip (line 1, column 60)");
}

ZEST_CASE(raw_value_alternative_takes_any_kind) {
    // RawValue decodes through a dispatch override that accepts any value, so
    // probing never judges it by its declared shape.
    std::variant<int, RawValue, bool> out;
    ZASSERT(json::from_string(R"("text")", out));
    ZASSERT(out.index() == 1U);
    ZEXPECT(std::get<RawValue>(out).data == R"("text")");

    ZASSERT(json::from_string("7", out));
    ZEXPECT(out.index() == 0U);
}

ZEST_CASE(optional_raw_value_alternative_takes_any_kind) {
    std::variant<std::optional<RawValue>, int> out;
    ZASSERT(json::from_string(R"("text")", out));
    ZASSERT(out.index() == 0U);
    const auto& raw = std::get<0>(out);
    ZASSERT(raw);
    ZEXPECT(raw->data == R"("text")");

    ZASSERT(json::from_string("null", out));
    ZASSERT(out.index() == 0U);
    ZEXPECT(!std::get<0>(out));
}

ZEST_CASE(adjacent_duplicate_tag_fails) {
    test::AdjacentShape out;
    auto status = json::from_string(R"({"t":"number","t":"point","c":42})", out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "adjacently tagged variant: duplicate tag field");
}

ZEST_CASE(internal_duplicate_tag_fails) {
    test::InternalShape out;
    auto status = json::from_string(R"({"kind":"circle","kind":"rect","radius":1})", out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "internally tagged variant: duplicate tag field");
}

ZEST_CASE(adjacent_duplicate_content_fails) {
    test::AdjacentShape out;
    auto status = json::from_string(R"({"t":"number","c":1,"c":2})", out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "adjacently tagged variant: duplicate content field");
}

};  // ZEST_SUITE(codec_json_decode)

}  // namespace

}  // namespace kota::codec
