#include <string>

#include "support/harness/glob.h"
#include "kota/zest/zest.h"

namespace kota {

namespace {

// `*` and `?` stay within one path segment: `*` matches any run of characters, `?` one.

ZEST_SUITE(support_glob_pattern_wildcard) {

ZEST_CASE(star_matches_any_run_within_a_segment) {
    test::expect_glob("*", {"", "foo", "bar.txt", "a"}, {"/", "foo/bar", "/foo", "x/y"});
    test::expect_glob("*.cc", {".cc", "foo.cc"}, {"foo.cpp", "a/b.cc"});
    test::expect_glob("*foo", {"foo", "barfoo"}, {"foox"});
    test::expect_glob("a*", {"a", "abc"}, {"abc/", "abc/d"});
}

ZEST_CASE(star_in_a_path) {
    test::expect_glob("*/b", {"a/b", "foo/b", "/b"}, {"a/c", "a/b/c", "a//b"});
    test::expect_glob("foo/*", {"foo/bar"}, {"foo/a/b", "foo"});
    test::expect_glob("*/*", {"a/b"}, {""});
}

ZEST_CASE(star_backtracks_to_the_segment_end) {
    // A mismatch at the segment's end retries the star rather than giving up.
    test::expect_glob("*a/b", {"aa/b", "xya/b"}, {"ab/b"});
    test::expect_glob("?*.cc", {"ab.cc"}, {"a/.cc", "a/b.cc"});
}

ZEST_CASE(dot_prefixed_names) {
    test::expect_glob(".*", {".git", ".hidden.txt"}, {"git", "hidden.txt", "path/.git"});
    test::expect_glob("._*", {"._git", "._hidden.txt"}, {"git", "path/._git"});
}

ZEST_CASE(embedded_double_star_is_a_single_star) {
    test::expect_glob("**.cpp", {"a.cpp"}, {"a/x/b.cpp"});
    test::expect_glob("a**b", {"ab", "axxb"}, {"a/x/b"});
    test::expect_glob("a**", {"a", "abc"}, {"a/b"});
    test::expect_glob("a**/", {"abc/"}, {"a", "a/b/"});
    test::expect_glob("foo**.cpp", {"foo.cpp"}, {"foo/x.cpp"});
    test::expect_glob("**include/test/*.{cc,hpp}",
                      {"include/test/fff.hpp", "xxx-yyy-include/test/fff.hpp"},
                      {"/include/test/aaa.cc", "a/include/test/bbb.cc"});
}

ZEST_CASE(question_matches_one_character) {
    test::expect_glob("?", {"a", "z", "0"}, {"", "ab", "/"});
    test::expect_glob("??", {"ab", "12"}, {"a", "abc"});
    test::expect_glob("?.?", {"a.b"}, {"ab.c", "a.bc"});
    test::expect_glob("?/b", {"a/b", "x/b"}, {"ab/b", "/b", "a//b"});
}

ZEST_CASE(question_does_not_match_an_empty_segment) {
    test::expect_glob("?/*", {"a/"}, {"a"});
    test::expect_glob("x*/*", {"x/y"}, {"x"});
}

ZEST_CASE(stars_retry_without_a_cutoff) {
    // Ported from rust-lang/glob's tests (MIT/Apache-2.0).
    test::expect_glob("a*b", {"a_b"});
    test::expect_glob("a*b*c", {"abc", "a_b_c", "a___b___c"}, {"abcd"});
    test::expect_glob("abc*abc*abc", {"abcabcabcabcabcabcabc"}, {"abcabcabcabcabcabcabca"});
    test::expect_glob("a*a*a*a*a*a*a*a*a", {std::string(31, 'a')});
    test::expect_glob("a*b[xyz]c*d", {"abxcdbxcddd"});
}

ZEST_CASE(long_segments_are_matched_through) {
    const std::string long_run(70000, 'b');
    test::expect_glob("{*a?Z,never}", {long_run + "a中Z"}, {long_run + "a中Y"});
    test::expect_glob("*Z", {long_run + "Z"}, {long_run + "Y"});
}

};  // ZEST_SUITE(support_glob_pattern_wildcard)

}  // namespace

}  // namespace kota
