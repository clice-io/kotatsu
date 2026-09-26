#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/configs.h"
#include "codec/harness/fixtures/repr.h"
#include "fixtures/attrs.h"
#include "fixtures/configs.h"
#include "fixtures/structs.h"
#include "fixtures/tagged.h"
#include "kota/zest/zest.h"
#include "kota/codec/toml/toml.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_toml_decode) {

ZEST_CASE(value_overload_takes_config) {
    auto result = toml::from_string<test::RenameAllTarget, test::CamelConfig>(R"(
itemId = 'abc'
totalScore = 1.5
userName = 2
)");
    ASSERT(result);
    EXPECT(result->user_name == 2);
    EXPECT(result->total_score == 1.5F);
    EXPECT(result->item_id == "abc");
}

ZEST_CASE(parse_error_fails_with_location) {
    // toml++ runs without exceptions, so its parse errors come back as values.
    test::Person out{};
    auto status = toml::from_string(R"(
name = "alice
age = 30
)",
                                    out);
    ASSERT(!status);
    EXPECT(zest::starts_with(status.error().message, "TOML parse error: "));
    ASSERT(status.error().location);
    EXPECT(status.error().location->line == 2U);
}

ZEST_CASE(type_mismatch_fails_at_its_value) {
    test::Person out{};
    auto status = toml::from_string(R"(
name = "alice"
age = "not_a_number"
)",
                                    out);
    ASSERT(!status);
    EXPECT(status.error().message == "invalid type: expected integer, got string");
    EXPECT(status.error().format_path() == "age");
    ASSERT(status.error().location);
    EXPECT(status.error().location->line == 3U);
    EXPECT(status.error().location->column == 7U);
}

ZEST_CASE(nested_type_mismatch_text_fails) {
    test::Person out{};
    auto status = toml::from_string(R"(
name = "alice"
age = 30
[addr]
city = "NY"
zip = "wrong"
)",
                                    out);
    ASSERT(!status);
    EXPECT(status.error().to_string() ==
           "invalid type: expected integer, got string at addr.zip (line 6, column 7)");
}

ZEST_CASE(unknown_field_fails_at_its_value) {
    test::Point out{};
    auto status = toml::from_string<test::StrictConfig>(R"(
x = 1
y = 2
extra = true
)",
                                                        out);
    ASSERT(!status);
    EXPECT(status.error().message == "unknown field 'extra'");
    ASSERT(status.error().location);
    EXPECT(status.error().location->line == 4U);
    EXPECT(status.error().location->column == 9U);
}

ZEST_CASE(integer_out_of_range_fails) {
    std::uint8_t out = 0;
    auto status = toml::from_string("__value = 300", out);
    ASSERT(!status);
    EXPECT(status.error().message == "integer value out of range");
}

ZEST_CASE(byte_out_of_range_fails) {
    std::vector<std::byte> out;
    auto status = toml::from_string("__value = [ 0, 256 ]", out);
    ASSERT(!status);
    EXPECT(status.error().message == "byte array element out of range [0, 255]");
}

ZEST_CASE(null_from_non_null_fails) {
    std::nullptr_t out = nullptr;
    auto status = toml::from_string("__value = 1", out);
    ASSERT(!status);
    EXPECT(status.error().message == "invalid type: expected null, got integer");
    ASSERT(status.error().location);
    EXPECT(status.error().location->line == 1U);
    EXPECT(status.error().location->column == 11U);
}

ZEST_CASE(char_reads_one_codepoint_up_to_255) {
    char out = '\0';
    ASSERT(toml::from_string("__value = 'é'", out));
    EXPECT(out == static_cast<char>(0xE9));
    ASSERT(toml::from_string("__value = 'ÿ'", out));
    EXPECT(out == static_cast<char>(0xFF));
}

ZEST_CASE(char_beyond_255_fails) {
    char out = '\0';
    auto status = toml::from_string("__value = 'Ā'", out);
    ASSERT(!status);
    EXPECT(status.error().message == codec::invalid_char_message);
    auto several = toml::from_string("__value = 'xy'", out);
    ASSERT(!several);
    EXPECT(several.error().message == codec::invalid_char_message);
}

ZEST_CASE(char_from_a_lone_high_octet_fails) {
    // A table built by hand can hold bytes that are not UTF-8, such as the
    // lone octet this backend once wrote for a char above 0x7F.
    toml::Table table{
        {"__value", "\xE9"}
    };
    char out = '\0';
    auto status = toml::from_toml(table, out);
    ASSERT(!status);
    EXPECT(status.error().message == codec::invalid_char_message);
}

ZEST_CASE(null_root_picks_monostate) {
    // The empty document is a null root, the one place a null reaches a
    // reader; an untagged variant probes it as any backend's null.
    auto read = toml::from_string<std::variant<int, std::monostate, std::string>>("");
    ASSERT(read);
    EXPECT(read->index() == 1U);
}

ZEST_CASE(null_root_no_match_fails) {
    auto status = toml::from_string<std::variant<int, std::string>>("");
    ASSERT(!status);
    EXPECT(status.error().message == "invalid type: expected string, got null");
}

ZEST_CASE(null_field_leaves_the_value_alone) {
    // A null field writes no key, so decoding it cannot reset the field: an
    // absent optional keeps what the value decoded into held.
    auto text = toml::to_string(test::Field<std::optional<int>>{});
    ASSERT(text);
    test::Field<std::optional<int>> out{5};
    ASSERT(toml::from_string(*text, out));
    EXPECT(out.value == std::optional<int>(5));
}

ZEST_CASE(null_in_a_required_field_does_not_read_back_fails) {
    // Known gap: the writer omits a null whose field the reader requires, so
    // the document it writes does not read back.
    auto none = toml::to_string(test::Field<std::variant<std::monostate, int>>{});
    ASSERT(none);
    test::Field<std::variant<std::monostate, int>> choice{7};
    auto status = toml::from_string(*none, choice);
    ASSERT(!status);
    EXPECT(status.error().message == "missing required field 'value'");

    auto unstamped = toml::to_string(test::Stamped{.stamp = {.tick = 0}});
    ASSERT(unstamped);
    test::Stamped stamped;
    auto repr_status = toml::from_string(*unstamped, stamped);
    ASSERT(!repr_status);
    EXPECT(repr_status.error().message == "missing required field 'stamp'");
}

ZEST_CASE(external_monostate_does_not_read_back_fails) {
    // Known gap: `{none = null}` loses its only key.
    auto text = toml::to_string(test::Field<test::ExternalShape>{});
    ASSERT(text);
    test::Field<test::ExternalShape> out;
    auto status = toml::from_string(*text, out);
    ASSERT(!status);
    EXPECT(status.error().message == "externally tagged variant: expected exactly one field");
}

ZEST_CASE(adjacent_monostate_does_not_read_back_fails) {
    // Known gap: `{t = 'none', c = null}` loses its content key.
    auto text = toml::to_string(test::Field<test::AdjacentShape>{});
    ASSERT(text);
    test::Field<test::AdjacentShape> out;
    auto status = toml::from_string(*text, out);
    ASSERT(!status);
    EXPECT(status.error().message == "adjacently tagged variant: missing content field");
}

ZEST_CASE(from_toml_reads_a_table) {
    toml::Table table{
        {"x", 1},
        {"y", 2}
    };
    test::Point out{};
    ASSERT(toml::from_toml(table, out));
    EXPECT(out == test::Point{.x = 1, .y = 2});
}

ZEST_CASE(table_root_reads_the_document) {
    toml::Table table{
        {"city", "shanghai"},
        {"zip",  200000    }
    };
    toml::Table out;
    ASSERT(toml::from_toml(table, out));
    // A toml::Table reads as a map to meta; its own operator== compares it.
    EXPECT((out == table));
}

ZEST_CASE(table_and_array_fields_pass_through) {
    // Read through the out parameter: the debug codec cannot print a
    // toml::Table, which a failed check on the value overload would show.
    test::Field<toml::Table> table{};
    ASSERT(toml::from_string("[value]\ncity = 'shanghai'\nzip = 200000", table));
    toml::Table expected_table{
        {"city", "shanghai"},
        {"zip",  200000    }
    };
    EXPECT((table.value == expected_table));
    test::Field<toml::Array> array{};
    ASSERT(toml::from_string("value = [ 'a', 'b' ]", array));
    toml::Array expected_array{"a", "b"};
    EXPECT((array.value == expected_array));
}

ZEST_CASE(table_field_from_a_scalar_fails) {
    test::Field<toml::Table> table{};
    auto status = toml::from_string("value = 2", table);
    ASSERT(!status);
    EXPECT(status.error().message == "invalid type: expected table, got integer");
    EXPECT(status.error().format_path() == "value");
}

};  // ZEST_SUITE(codec_toml_decode)

}  // namespace

}  // namespace kota::codec
