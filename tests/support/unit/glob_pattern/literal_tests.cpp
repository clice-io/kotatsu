#include <string>

#include "support/harness/glob.h"
#include "kota/zest/zest.h"

namespace kota {

namespace {

// Patterns without wildcards, or whose wildcards are escaped: they match their own text,
// whole, and nothing else.

ZEST_SUITE(support_glob_pattern_literal) {

ZEST_CASE(names_match_exactly) {
    test::expect_glob("node_modules",
                      {"node_modules"},
                      {"node_module", "/node_modules", "test/node_modules", ""});
    test::expect_glob("test.txt", {"test.txt"}, {"test?txt", "/text.txt", "test/test.txt"});
    test::expect_glob("test(.txt", {"test(.txt"}, {"test?txt"});
    test::expect_glob("qunit", {"qunit"}, {"qunit.css", "test/qunit"});
    test::expect_glob("a", {"a"}, {"b", "ab", ""});
}

ZEST_CASE(paths_match_exactly) {
    test::expect_glob("foo/bar", {"foo/bar"}, {"foo/baz", "foo/bar/baz", "foobar"});
    test::expect_glob("a/b/c/d", {"a/b/c/d"}, {"a/b/c", "a/b/c/d/e"});
    test::expect_glob("/", {"/"}, {"", "//"});
}

ZEST_CASE(empty_pattern_matches_only_the_empty_path) {
    test::expect_glob("", {""}, {"foo", "/"});
}

ZEST_CASE(trailing_slash_is_literal) {
    test::expect_glob("foo/", {"foo/"}, {"foo", "foo/bar"});
}

ZEST_CASE(escaped_wildcards_are_literal) {
    test::expect_glob(R"(\*star)", {"*star"}, {"xstar"});
    test::expect_glob(R"(\{\*\})", {"{*}"}, {"{x}"});
    test::expect_glob(R"(\中\文\*\?\[\{\\)", {R"(中文*?[{\)"}, {R"(中文*?[{\x)", R"(中文x?[{\)"});
}

ZEST_CASE(escaped_backslash_before_a_separator) {
    // The first backslash escapes the second; the slash is a real separator.
    test::expect_glob(R"(\\/)", {R"(\/)"}, {"/"});
    test::expect_glob(R"(**/\\/目标)", {R"(x/\/目标)"}, {"x/目标"});
}

ZEST_CASE(backslashes_in_paths_are_ordinary_characters) {
    test::expect_glob("?", {R"(\)"});
    test::expect_glob("*", {R"(a\b)", R"(\)"});
    test::expect_glob("**/*.txt", {R"(path\with\backslash.txt)"});
}

ZEST_CASE(literal_prefix_with_escapes) {
    test::expect_glob(R"(/work/项目\[demo\]/src/**/*.cpp)",
                      {"/work/项目[demo]/src/test.cpp", "/work/项目[demo]/src/目录/test.cpp"},
                      {"/work/项目[demo]/srcx/test.cpp", "/work/项目d/src/test.cpp"});
}

ZEST_CASE(literal_prefix_ends_mid_segment) {
    // The prefix `a` and the segment after it are one input segment.
    test::expect_glob("a?", {"ab"}, {"a/", "a"});
    test::expect_glob("a[bc]", {"ab"}, {"a/b"});
    test::expect_glob("中?", {"中文"}, {"x文"});
}

ZEST_CASE(nul_bytes_are_ordinary_characters) {
    const std::string pattern("**/中*\0文", 11);
    test::expect_glob(pattern, {std::string("中\0文", 7), std::string("x/中y\0文", 10)}, {"中文"});
}

};  // ZEST_SUITE(support_glob_pattern_literal)

}  // namespace

}  // namespace kota
