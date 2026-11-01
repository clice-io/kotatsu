#include <array>
#include <string>
#include <string_view>

#include "kotatsu/zest/zest.h"
#include "kotatsu/support/naming.h"

namespace kotatsu::naming {

namespace {

constexpr std::array<std::string_view, 3> declared = {"read_only", "FullControl", "HTTPServer"};
constexpr std::array<std::string_view, 2> alike = {"FooBar", "foo_bar"};

ZEST_SUITE(support_naming) {

ZEST_CASE(ascii_character_classes) {
    ZEXPECT(is_lower('a'));
    ZEXPECT(!is_lower('A'));
    ZEXPECT(is_upper('Z'));
    ZEXPECT(!is_upper('z'));
    ZEXPECT(is_digit('7'));
    ZEXPECT(!is_digit('x'));
    ZEXPECT(is_alpha('q'));
    ZEXPECT(!is_alpha('_'));
    ZEXPECT(is_alnum('0'));
    ZEXPECT(!is_alnum('-'));
    ZEXPECT(is_non_ascii('\xC3'));
    ZEXPECT(!is_non_ascii('~'));
}

ZEST_CASE(ascii_case_conversion) {
    ZEXPECT(to_lower('Q') == 'q');
    ZEXPECT(to_lower('q') == 'q');
    ZEXPECT(to_lower('1') == '1');
    ZEXPECT(to_upper('q') == 'Q');
    ZEXPECT(to_upper('\xC3') == '\xC3');
}

ZEST_CASE(lower_snake_splits_at_case_changes) {
    ZEXPECT(normalize_to_lower_snake("fooBar") == "foo_bar");
    ZEXPECT(normalize_to_lower_snake("FooBar") == "foo_bar");
    ZEXPECT(normalize_to_lower_snake("foo_bar") == "foo_bar");
    ZEXPECT(normalize_to_lower_snake("HTTPServer") == "http_server");
    ZEXPECT(normalize_to_lower_snake("XMLHttpRequest") == "xml_http_request");
    ZEXPECT(normalize_to_lower_snake("ABC") == "abc");
}

ZEST_CASE(lower_snake_splits_after_digits) {
    ZEXPECT(normalize_to_lower_snake("HTTPServer2Go") == "http_server2_go");
    ZEXPECT(normalize_to_lower_snake("v2Beta") == "v2_beta");
    ZEXPECT(normalize_to_lower_snake("item2") == "item2");
}

ZEST_CASE(lower_snake_joins_separators_into_one) {
    ZEXPECT(normalize_to_lower_snake("foo-bar baz") == "foo_bar_baz");
    ZEXPECT(normalize_to_lower_snake("foo__bar") == "foo_bar");
    ZEXPECT(normalize_to_lower_snake("__init__") == "init");
    ZEXPECT(normalize_to_lower_snake("-_-").empty());
    ZEXPECT(normalize_to_lower_snake("").empty());
}

ZEST_CASE(lower_snake_keeps_utf8_letters) {
    ZEXPECT(normalize_to_lower_snake("naïveApi") == "naïve_api");
    ZEXPECT(normalize_to_lower_snake("größe") == "größe");
    ZEXPECT(normalize_to_lower_snake("名前Value") == "名前_value");
    // A UTF-8 byte has no case: it does not end a run of capitals.
    ZEXPECT(normalize_to_lower_snake("HTTP名前") == "http名前");
}

ZEST_CASE(camel_case_from_any_spelling) {
    ZEXPECT(snake_to_camel("foo_bar", false) == "fooBar");
    ZEXPECT(snake_to_camel("foo_bar", true) == "FooBar");
    ZEXPECT(snake_to_camel("FooBar", false) == "fooBar");
    ZEXPECT(snake_to_camel("http_server", true) == "HttpServer");
    ZEXPECT(snake_to_camel("x", true) == "X");
    ZEXPECT(snake_to_camel("", true).empty());
}

ZEST_CASE(camel_case_cannot_mark_a_word_starting_with_a_digit) {
    ZEXPECT(snake_to_camel("version_2", false) == "version2");
    ZEXPECT(snake_to_camel("2nd_try", true) == "2ndTry");
}

ZEST_CASE(snake_to_upper_spells_upper_snake_case) {
    ZEXPECT(snake_to_upper("fooBar") == "FOO_BAR");
    ZEXPECT(snake_to_upper("http_server") == "HTTP_SERVER");
}

ZEST_CASE(identifier_from_any_text) {
    ZEXPECT(normalize_identifier("foo-bar.baz") == "foo_bar_baz");
    ZEXPECT(normalize_identifier("a  b") == "a_b");
    ZEXPECT(normalize_identifier("_keep") == "_keep");
    ZEXPECT(normalize_identifier("trailing__") == "trailing");
    ZEXPECT(normalize_identifier("9lives") == "_9lives");
    ZEXPECT(normalize_identifier("") == "unnamed");
    ZEXPECT(normalize_identifier("--") == "unnamed");
}

ZEST_CASE(policies_spell_names_at_compile_time) {
    ZSTATIC_EXPECT(rename_policy::identity{}("fooBar") == "fooBar");
    ZSTATIC_EXPECT(rename_policy::lower_snake{}("fooBar") == "foo_bar");
    ZSTATIC_EXPECT(rename_policy::lower_camel{}("foo_bar") == "fooBar");
    ZSTATIC_EXPECT(rename_policy::upper_camel{}("foo_bar") == "FooBar");
    ZSTATIC_EXPECT(rename_policy::upper_snake{}("fooBar") == "FOO_BAR");
}

ZEST_CASE(casing_names_its_policy) {
    ZSTATIC_EXPECT(casing_fn(Casing::Identity) == &spell_with<rename_policy::identity>);
    ZSTATIC_EXPECT(casing_fn(Casing::LowerSnake) == &spell_with<rename_policy::lower_snake>);
    ZSTATIC_EXPECT(casing_fn(Casing::LowerCamel) == &spell_with<rename_policy::lower_camel>);
    ZSTATIC_EXPECT(casing_fn(Casing::UpperCamel) == &spell_with<rename_policy::upper_camel>);
    ZSTATIC_EXPECT(casing_fn(Casing::UpperSnake) == &spell_with<rename_policy::upper_snake>);
    // A policy's rename_fn names the policy again.
    ZEXPECT(zest::type_eq<rename_policy_of_t<casing_fn(Casing::UpperCamel)>,
                          rename_policy::upper_camel>());
}

ZEST_CASE(renamed_spells_a_table_into_static_storage) {
    constexpr const auto& names = renamed<rename_policy::lower_camel, declared>;
    ZSTATIC_EXPECT(names.size() == 3U);
    ZSTATIC_EXPECT(names[0] == "readOnly");
    ZSTATIC_EXPECT(names[1] == "fullControl");
    ZSTATIC_EXPECT(names[2] == "httpServer");
    // The identity policy keeps the names as they are, where they are.
    ZSTATIC_EXPECT(&renamed<rename_policy::identity, declared> == &declared);
}

ZEST_CASE(distinct_finds_a_name_listed_twice) {
    constexpr std::array<std::string_view, 3> twice = {"a", "b", "a"};
    ZSTATIC_EXPECT(distinct(declared));
    ZSTATIC_EXPECT(!distinct(twice));
    // Two names a policy spells alike.
    ZSTATIC_EXPECT(!distinct(renamed<rename_policy::lower_snake, alike>));
}

ZEST_CASE(renames_in_constant_evaluation) {
    ZSTATIC_EXPECT(normalize_to_lower_snake("fooBar") == "foo_bar");
    ZSTATIC_EXPECT(snake_to_camel("foo_bar", true) == "FooBar");
    ZSTATIC_EXPECT(normalize_identifier("1x") == "_1x");
}

};  // ZEST_SUITE(support_naming)

}  // namespace

}  // namespace kotatsu::naming
