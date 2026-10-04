#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/attrs.h"
#include "codec/harness/fixtures/configs.h"
#include "codec/harness/fixtures/containers.h"
#include "codec/harness/fixtures/repr.h"
#include "codec/harness/fixtures/structs.h"
#include "codec/harness/fixtures/tagged.h"
#include "fixtures/attrs.h"
#include "fixtures/configs.h"
#include "kota/zest/zest.h"
#include "kota/meta/compare.h"
#include "kota/codec/toml/toml.h"

namespace kota::codec {

namespace {

/// Whether the value-returning overload takes T.
template <typename T>
concept decodes_by_value = requires(std::string_view text) { toml::from_string<T>(text); };

ZEST_SUITE(codec_toml_decode) {

ZEST_CASE(value_overload_value_initializes) {
    // `T value{}` would copy-list-initialize the explicit list from `{}`.
    auto result = toml::from_string<test::HoldsExplicit>(R"(
count = 2
list = [1, 2]
)");
    ASSERT(result);
    const test::HoldsExplicit expected{
        .list = {1, 2},
        .count = 2
    };
    EXPECT(meta::eq(*result, expected));
    STATIC_EXPECT(decodes_by_value<test::HoldsExplicit>);
    // A type with no default constructor has no value to decode into.
    STATIC_EXPECT(!decodes_by_value<test::NoDefault>);
}

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
    // toml++'s description follows the prefix.
    constexpr std::string_view prefix = "TOML parse error: ";
    EXPECT(zest::starts_with(status.error().message, prefix));
    EXPECT(status.error().message.size() > prefix.size());
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

ZEST_CASE(unknown_field_fails_at_its_key) {
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
    EXPECT(status.error().location->column == 1U);
}

ZEST_CASE(unknown_fields_reported_at_their_keys) {
    UnknownFields unknown;
    scoped_context<UnknownFields> scope(unknown);
    test::Person out{};
    auto status = toml::from_string(R"(
name = "alice"
nick = "al"
age = 30

[addr]
city = "NY"
zip = 10001
floor = 3
)",
                                    out);
    ASSERT(status);
    EXPECT(out.addr.zip == 10001);
    // A table's keys are read in order of their names: addr before nick.
    ASSERT(unknown.entries.size() == 2U);
    EXPECT(unknown.entries[0].to_string() == "unknown field 'floor' at addr (line 9, column 1)");
    EXPECT(unknown.entries[1].to_string() == "unknown field 'nick' (line 3, column 1)");
}

ZEST_CASE(unknown_fields_in_arrays_of_tables_and_dotted_keys_reported_at_their_keys) {
    UnknownFields unknown;
    scoped_context<UnknownFields> scope(unknown);
    test::Layout out{};
    auto status = toml::from_string(R"(
id = 1
origin.x = 1
origin.y = 2
origin.z = 0

[[points]]
x = 3
y = 4

[[points]]
x = 5
w = 0
y = 6

[named]
)",
                                    out);
    ASSERT(status);
    ASSERT(unknown.entries.size() == 2U);
    EXPECT(unknown.entries[0].to_string() == "unknown field 'z' at origin (line 5, column 8)");
    EXPECT(unknown.entries[1].to_string() == "unknown field 'w' at points[1] (line 13, column 1)");
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
    // Two bytes of UTF-8 under either lead byte: C2 for U+0080-U+00BF, C3
    // above.
    char out = '\0';
    ASSERT(toml::from_string(R"(__value = "\u0080")", out));
    EXPECT(out == static_cast<char>(0x80));
    ASSERT(toml::from_string("__value = '§'", out));
    EXPECT(out == static_cast<char>(0xA7));
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

ZEST_CASE(char_from_bytes_that_are_not_utf8_fails) {
    // A table built by hand can hold bytes that are not UTF-8: the lone
    // octet this backend once wrote for a char above 0x7F, or a lead byte
    // followed by one that does not continue it.
    for(std::string_view text: {"\xE9", "\xC3\x28"}) {
        ZEST_CONTEXT("text: {}", text);
        toml::Table table{
            {"__value", text}
        };
        char out = '\0';
        auto status = toml::from_toml(table, out);
        ASSERT(!status);
        EXPECT(status.error().message == codec::invalid_char_message);
    }
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
