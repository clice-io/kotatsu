#include <cstddef>
#include <string_view>

#include "support/harness/glob.h"
#include "kota/zest/zest.h"
#include "kota/support/glob_pattern.h"

namespace kota {

namespace {

// Each error spans the offending bytes of the pattern as written, whether they sit in the
// literal prefix, after it, or inside a brace alternative.

ZEST_SUITE(support_glob_pattern_syntax) {

ZEST_CASE(unmatched_bracket_fails) {
    test::expect_glob_error("[a-z", GlobError::UnmatchedBracket, 0, 1);
    test::expect_glob_error("foo.[a-z", GlobError::UnmatchedBracket, 4, 5);
    test::expect_glob_error("*[", GlobError::UnmatchedBracket, 1, 2);
    test::expect_glob_error("{a,[}", GlobError::UnmatchedBracket, 3, 4);
    test::expect_glob_error("{[abc\\]}", GlobError::UnmatchedBracket, 1, 2);
    // A `]` right after `[`, or after its negation, is a member, not the end.
    test::expect_glob_error("[]", GlobError::UnmatchedBracket, 0, 1);
    test::expect_glob_error("[!]", GlobError::UnmatchedBracket, 0, 1);
    test::expect_glob_error("x[^]", GlobError::UnmatchedBracket, 1, 2);
    // An escaped `]` is a member too.
    test::expect_glob_error(R"([a-\])", GlobError::UnmatchedBracket, 0, 1);
}

ZEST_CASE(stray_backslash_fails) {
    test::expect_glob_error("foo\\", GlobError::StrayBackslash, 3, 4);
    test::expect_glob_error("*\\", GlobError::StrayBackslash, 1, 2);
    test::expect_glob_error("{a}\\", GlobError::StrayBackslash, 3, 4);
    test::expect_glob_error(R"(a\[b\)", GlobError::StrayBackslash, 4, 5);
    test::expect_glob_error("x*[\\", GlobError::StrayBackslash, 3, 4);
}

ZEST_CASE(nested_brace_fails) {
    test::expect_glob_error("{a,{b,c}}", GlobError::NestedBrace, 3, 4);
}

ZEST_CASE(empty_brace_fails) {
    test::expect_glob_error("foo.{}", GlobError::EmptyBrace, 4, 6);
}

ZEST_CASE(incomplete_brace_fails) {
    test::expect_glob_error("{foo,bar", GlobError::IncompleteBrace, 0, 1);
    test::expect_glob_error("xx{a", GlobError::IncompleteBrace, 2, 3);
    // The backslash escapes the closing brace.
    test::expect_glob_error("{foo\\}", GlobError::IncompleteBrace, 0, 1);
}

ZEST_CASE(inverted_range_fails) {
    test::expect_glob_error("[z-a]", GlobError::InvalidRange, 1, 4);
    test::expect_glob_error("[!z-a]", GlobError::InvalidRange, 2, 5);
    test::expect_glob_error("pre[z-a]", GlobError::InvalidRange, 4, 7);
    test::expect_glob_error("{a,[z-a]}", GlobError::InvalidRange, 4, 7);
    test::expect_glob_error("xx{a,[z-a]}", GlobError::InvalidRange, 6, 9);
    test::expect_glob_error("{a,b}[z-a]", GlobError::InvalidRange, 6, 9);
    test::expect_glob_error("[a-c\\z-\\a]", GlobError::InvalidRange, 4, 9);
}

ZEST_CASE(double_slash_fails) {
    test::expect_glob_error("//foo", GlobError::MultipleSlash, 0, 2);
    test::expect_glob_error("foo//bar", GlobError::MultipleSlash, 3, 5);
    test::expect_glob_error(R"(a\[//b)", GlobError::MultipleSlash, 3, 5);
    test::expect_glob_error("**/foo//*.cc", GlobError::MultipleSlash, 6, 8);
    test::expect_glob_error("/foo/bar/baz////aaa.{c,cc}", GlobError::MultipleSlash, 12, 14);
    test::expect_glob_error("abc/{d,e//f}", GlobError::MultipleSlash, 8, 10);
    test::expect_glob_error("x{a/,b}/c", GlobError::MultipleSlash, 3, 8);
}

ZEST_CASE(alternative_starting_with_a_slash_after_the_prefix_fails) {
    // Expansion is textual: the prefix's `/` and the alternative's spell `//`, and the span
    // runs from one to the other.
    test::expect_glob_error("a/{/}", GlobError::MultipleSlash, 1, 4);
    test::expect_glob_error("a/{/b}", GlobError::MultipleSlash, 1, 4);
    test::expect_glob_error("a/{b,/c}", GlobError::MultipleSlash, 1, 6);
    test::expect_glob_error(R"(a\*/{/})", GlobError::MultipleSlash, 3, 6);
    // Without a prefix's `/` before it, a leading `/` is fine.
    test::expect_glob("{/a}", {"/a"}, {"a"});
    test::expect_glob("a{/b}", {"a/b"}, {"ab"});
}

ZEST_CASE(triple_star_fails) {
    test::expect_glob_error("***.js", GlobError::MultipleStar, 0, 3);
    test::expect_glob_error("a/****", GlobError::MultipleStar, 2, 5);
    test::expect_glob_error("**/****.{c,cc}", GlobError::MultipleStar, 3, 6);
    test::expect_glob("**.js", {"a.js"});
}

ZEST_CASE(escaped_slash_fails) {
    test::expect_glob_error(R"(\/)", GlobError::InvalidEscape, 0, 2);
    test::expect_glob_error(R"(a\/b)", GlobError::InvalidEscape, 1, 3);
    test::expect_glob_error(R"(**\/a)", GlobError::InvalidEscape, 2, 4);
    test::expect_glob_error(R"({a\/b,c})", GlobError::InvalidEscape, 2, 4);
    test::expect_glob_error(R"({abc,d\/e})", GlobError::InvalidEscape, 6, 8);
    test::expect_glob_error(R"(a\[\/b)", GlobError::InvalidEscape, 3, 5);
}

ZEST_CASE(invalid_utf8_fails) {
    test::expect_glob_error("\x80", GlobError::InvalidUtf8, 0, 1);
    test::expect_glob_error("caf\xC3", GlobError::InvalidUtf8, 3, 4);
    test::expect_glob_error("caf\xE9*", GlobError::InvalidUtf8, 3, 4);
    // Overlong, surrogate and truncated sequences.
    test::expect_glob_error("\xC0\x80", GlobError::InvalidUtf8, 0, 1);
    test::expect_glob_error("\xED\xA0\x80", GlobError::InvalidUtf8, 0, 1);
    test::expect_glob_error("\xE1\x80\x41", GlobError::InvalidUtf8, 0, 1);
    test::expect_glob_error("a[\xFF]", GlobError::InvalidUtf8, 2, 3);
}

ZEST_CASE(error_in_a_later_alternative_fails) {
    test::expect_glob_error("{ok,***}", GlobError::MultipleStar, 4, 7);
    test::expect_glob_error("{a,b}{c,[}", GlobError::UnmatchedBracket, 8, 9);
}

ZEST_CASE(expansion_past_the_limit_fails) {
    auto product = GlobPattern::create("{a,b}.{c,d}", 2);
    ASSERT(!product.has_value());
    EXPECT(product.error().kind == GlobError::TooManyExpansions);
    auto single = GlobPattern::create("{a,b,c}", 2);
    ASSERT(!single.has_value());
    EXPECT(single.error().kind == GlobError::TooManyExpansions);
}

ZEST_CASE(expansions_up_to_the_limit_compile) {
    auto product = GlobPattern::create("{a,b}.{c,d}", 4);
    ASSERT(product.has_value());
    EXPECT(product->match("b.c"));
    auto single = GlobPattern::create("{a}", 1);
    ASSERT(single.has_value());
    EXPECT(single->match("a"));
}

ZEST_CASE(limit_of_zero_reads_braces_literally) {
    auto literal = GlobPattern::create("{a,b}", 0);
    ASSERT(literal.has_value());
    EXPECT(literal->match("{a,b}"));
    EXPECT(!literal->match("a"));
    auto nested = GlobPattern::create("x{a,{b}}*", 0);
    ASSERT(nested.has_value());
    EXPECT(nested->match("x{a,{b}}yz"));
}

};  // ZEST_SUITE(support_glob_pattern_syntax)

}  // namespace

}  // namespace kota
