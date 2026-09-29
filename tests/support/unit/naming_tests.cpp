#include <string>

#include "kota/zest/zest.h"
#include "kota/support/naming.h"

namespace kota::naming {

namespace {

ZEST_SUITE(support_naming) {

ZEST_CASE(ascii_character_classes) {
    EXPECT(is_lower('a'));
    EXPECT(!is_lower('A'));
    EXPECT(is_upper('Z'));
    EXPECT(!is_upper('z'));
    EXPECT(is_digit('7'));
    EXPECT(!is_digit('x'));
    EXPECT(is_alpha('q'));
    EXPECT(!is_alpha('_'));
    EXPECT(is_alnum('0'));
    EXPECT(!is_alnum('-'));
    EXPECT(is_non_ascii('\xC3'));
    EXPECT(!is_non_ascii('~'));
}

ZEST_CASE(ascii_case_conversion) {
    EXPECT(to_lower('Q') == 'q');
    EXPECT(to_lower('q') == 'q');
    EXPECT(to_lower('1') == '1');
    EXPECT(to_upper('q') == 'Q');
    EXPECT(to_upper('\xC3') == '\xC3');
}

ZEST_CASE(lower_snake_splits_at_case_changes) {
    EXPECT(normalize_to_lower_snake("fooBar") == "foo_bar");
    EXPECT(normalize_to_lower_snake("FooBar") == "foo_bar");
    EXPECT(normalize_to_lower_snake("foo_bar") == "foo_bar");
    EXPECT(normalize_to_lower_snake("HTTPServer") == "http_server");
    EXPECT(normalize_to_lower_snake("XMLHttpRequest") == "xml_http_request");
    EXPECT(normalize_to_lower_snake("ABC") == "abc");
}

ZEST_CASE(lower_snake_splits_after_digits) {
    EXPECT(normalize_to_lower_snake("HTTPServer2Go") == "http_server2_go");
    EXPECT(normalize_to_lower_snake("v2Beta") == "v2_beta");
    EXPECT(normalize_to_lower_snake("item2") == "item2");
}

ZEST_CASE(lower_snake_joins_separators_into_one) {
    EXPECT(normalize_to_lower_snake("foo-bar baz") == "foo_bar_baz");
    EXPECT(normalize_to_lower_snake("foo__bar") == "foo_bar");
    EXPECT(normalize_to_lower_snake("__init__") == "init");
    EXPECT(normalize_to_lower_snake("-_-").empty());
    EXPECT(normalize_to_lower_snake("").empty());
}

ZEST_CASE(lower_snake_keeps_utf8_letters) {
    EXPECT(normalize_to_lower_snake("naïveApi") == "naïve_api");
    EXPECT(normalize_to_lower_snake("größe") == "größe");
    EXPECT(normalize_to_lower_snake("名前Value") == "名前_value");
}

ZEST_CASE(camel_case_from_any_spelling) {
    EXPECT(snake_to_camel("foo_bar", false) == "fooBar");
    EXPECT(snake_to_camel("foo_bar", true) == "FooBar");
    EXPECT(snake_to_camel("FooBar", false) == "fooBar");
    EXPECT(snake_to_camel("http_server", true) == "HttpServer");
    EXPECT(snake_to_camel("x", true) == "X");
    EXPECT(snake_to_camel("", true).empty());
}

ZEST_CASE(camel_case_cannot_mark_a_word_starting_with_a_digit) {
    EXPECT(snake_to_camel("version_2", false) == "version2");
    EXPECT(snake_to_camel("2nd_try", true) == "2ndTry");
}

ZEST_CASE(snake_to_upper_spells_upper_snake_case) {
    EXPECT(snake_to_upper("fooBar") == "FOO_BAR");
    EXPECT(snake_to_upper("http_server") == "HTTP_SERVER");
}

ZEST_CASE(identifier_from_any_text) {
    EXPECT(normalize_identifier("foo-bar.baz") == "foo_bar_baz");
    EXPECT(normalize_identifier("a  b") == "a_b");
    EXPECT(normalize_identifier("_keep") == "_keep");
    EXPECT(normalize_identifier("trailing__") == "trailing");
    EXPECT(normalize_identifier("9lives") == "_9lives");
    EXPECT(normalize_identifier("") == "unnamed");
    EXPECT(normalize_identifier("--") == "unnamed");
}

ZEST_CASE(policies_rename_for_serialization) {
    EXPECT(rename_policy::identity{}(true, "fooBar") == "fooBar");
    EXPECT(rename_policy::lower_snake{}(true, "fooBar") == "foo_bar");
    EXPECT(rename_policy::lower_camel{}(true, "foo_bar") == "fooBar");
    EXPECT(rename_policy::upper_camel{}(true, "foo_bar") == "FooBar");
    EXPECT(rename_policy::upper_snake{}(true, "fooBar") == "FOO_BAR");
}

ZEST_CASE(policies_read_back_to_lower_snake) {
    EXPECT(rename_policy::identity{}(false, "fooBar") == "fooBar");
    EXPECT(rename_policy::lower_snake{}(false, "FooBar") == "foo_bar");
    EXPECT(rename_policy::lower_camel{}(false, "fooBar") == "foo_bar");
    EXPECT(rename_policy::upper_camel{}(false, "FooBar") == "foo_bar");
    EXPECT(rename_policy::upper_snake{}(false, "FOO_BAR") == "foo_bar");
}

ZEST_CASE(casing_names_its_policy) {
    EXPECT(zest::type_eq<rename_policy_t<Casing::Identity>, rename_policy::identity>());
    EXPECT(zest::type_eq<rename_policy_t<Casing::LowerSnake>, rename_policy::lower_snake>());
    EXPECT(zest::type_eq<rename_policy_t<Casing::LowerCamel>, rename_policy::lower_camel>());
    EXPECT(zest::type_eq<rename_policy_t<Casing::UpperCamel>, rename_policy::upper_camel>());
    EXPECT(zest::type_eq<rename_policy_t<Casing::UpperSnake>, rename_policy::upper_snake>());
}

ZEST_CASE(renames_in_constant_evaluation) {
    STATIC_EXPECT(normalize_to_lower_snake("fooBar") == "foo_bar");
    STATIC_EXPECT(snake_to_camel("foo_bar", true) == "FooBar");
    STATIC_EXPECT(normalize_identifier("1x") == "_1x");
}

};  // ZEST_SUITE(support_naming)

}  // namespace

}  // namespace kota::naming
