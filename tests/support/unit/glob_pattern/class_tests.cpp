#include <string>

#include "support/harness/glob.h"
#include "kota/zest/zest.h"

namespace kota {

namespace {

// Bracket expressions: one character from a set of members and ranges, or, negated with `!`
// or `^`, one not in it. None matches `/`.

ZEST_SUITE(support_glob_pattern_class) {

ZEST_CASE(members_and_ranges_match_one_character) {
    test::expect_glob("foo.[0-9]", {"foo.5", "foo.8"}, {"bar.5", "foo.f"});
    test::expect_glob(R"([a-zA-Z\]])", {"]", "s", "S"}, {"[", "0"});
    test::expect_glob(R"([\\^a-zA-Z""\\])", {"\"", "^", "\\", "x", "X"}, {"0"});
    test::expect_glob(R"([\*-\^])", {"*", "A", "Z", "\\", "^", "-"}, {"a", "z"});
}

ZEST_CASE(negation_with_bang_or_caret) {
    test::expect_glob("foo.[!0-9]", {"foo.f"}, {"foo.5", "foo.8", "bar.5"});
    test::expect_glob("foo.[^0-9]", {"foo.f"}, {"foo.5", "foo.8", "bar.5"});
    test::expect_glob(R"([!0-9a-fA-F\-+\*])", {"s", "S", "H", "]"}, {"1", "*"});
    test::expect_glob(R"([^\^0-9a-fA-F\-+\*])", {"s", "S", "H", "]"}, {"1", "*", "^"});
}

ZEST_CASE(negation_marks_only_in_first_place) {
    test::expect_glob("foo.[0!^*?]", {"foo.0", "foo.!", "foo.^", "foo.*", "foo.?"}, {"foo.5"});
}

ZEST_CASE(never_matches_a_slash) {
    test::expect_glob("foo[/]bar", {}, {"foo/bar"});
    test::expect_glob("[!a]", {"b", "z", "0"}, {"a", "/"});
    test::expect_glob("[!0-9]", {"a"}, {"5", "/"});
    test::expect_glob("[^a-z]", {"0", "A"}, {"a", "/"});
    // An escaped slash is accepted as a member, but still never matches.
    test::expect_glob(R"([\/])", {}, {"/", "a"});
    test::expect_glob(R"(**/[\/]*/目标)", {}, {"x//目标"});
}

ZEST_CASE(closing_bracket_first_is_a_member) {
    test::expect_glob("[]]", {"]"}, {"a"});
    test::expect_glob("foo.[]-]", {"foo.]", "foo.-"});
    test::expect_glob("foo.[][!]", {"foo.]", "foo.[", "foo.!"});
    test::expect_glob("[!]]", {"a"}, {"]", "a]"});
    test::expect_glob("[^]]", {"a", "0"}, {"]", "a]", "/]"});
    test::expect_glob("{[!]],[]]}", {"a", "]"}, {"/"});
}

ZEST_CASE(opening_bracket_is_a_member) {
    test::expect_glob("foo.[[]", {"foo.["});
}

ZEST_CASE(dash_first_last_or_after_a_range_is_a_member) {
    for(auto pattern: {"[abc-]", "[-abc]", "[a-c-]"}) {
        test::expect_glob(pattern, {"a", "b", "c", "-"}, {"d"});
    }
    test::expect_glob("[a-c-e]", {"b", "-", "e"}, {"d"});
    test::expect_glob("[-]", {"-"}, {"a"});
    test::expect_glob("[!-]", {"a"}, {"-"});
}

ZEST_CASE(members_in_any_order) {
    // Ported from rust-lang/glob's tests (MIT/Apache-2.0).
    for(auto pattern: {"[a-z123]", "[1a-z23]", "[123a-z]"}) {
        std::string letter = "a";
        for(char c = 'a'; c <= 'z'; ++c) {
            letter[0] = c;
            test::expect_glob(pattern, {letter, "1", "2", "3"}, {"4", "A"});
        }
    }
    std::string digit = "a0b";
    for(char c = '0'; c <= '9'; ++c) {
        digit[1] = c;
        test::expect_glob("a[0-9]b", {digit});
        test::expect_glob("a[!0-9]b", {"a_b"}, {digit});
    }
}

ZEST_CASE(disjoint_and_overlapping_ranges) {
    test::expect_glob("[a-cx-z]", {"b", "y"}, {"d"});
    test::expect_glob("[!a-cx-z]", {"d"}, {"b"});
    test::expect_glob("[aaa]", {"a"}, {"b"});
    test::expect_glob("[a-mh-z]", {"h", "z"}, {"A"});
}

ZEST_CASE(classes_within_names) {
    test::expect_glob("[a-z]oo", {"foo", "boo"}, {"Foo", "1oo", "aoo/bar"});
    test::expect_glob("[0-9]*", {"1foo", "9"}, {"a1"});
    test::expect_glob("*foo[0-9a-z].{c,cpp,cppm,?pp}",
                      {"foo2.cpp", "foo3.cppm", "foot.cppm", "foot.hpp", "foot.app"},
                      {"foo1.cc", "fooD.cppm", "BarfooD.cppm", "foofooD.cppm"});
    test::expect_glob("src/*/[中a]?*.cpp",
                      {std::string("src/x/a") + '\xff' + ".cpp"},
                      {"src/x/a.cpp"});
}

ZEST_CASE(slash_in_a_class_is_not_a_separator) {
    test::expect_glob("**/[a/]b/目标", {"x/ab/目标"}, {"x//b/目标"});
    test::expect_glob("**/[a/]*b", {"aaaa/ac/ab"}, {"aaaa/ac/ac"});
}

};  // ZEST_SUITE(support_glob_pattern_class)

}  // namespace

}  // namespace kota
