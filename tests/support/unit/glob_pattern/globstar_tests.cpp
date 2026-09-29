#include <string>

#include "support/harness/glob.h"
#include "kota/zest/zest.h"

namespace kota {

namespace {

// `**` as a whole segment matches any number of whole segments, none included; it can
// absorb the separator on one side of it, not both.

ZEST_SUITE(support_glob_pattern_globstar) {

ZEST_CASE(alone_matches_everything) {
    test::expect_glob("**", {"", "foo", "foo/bar/baz", "/", "folder/foo/", "/node_modules/foo.js"});
    test::expect_glob("**/", {"foo/bar", "foo", "/"});
}

ZEST_CASE(leading_matches_at_any_depth) {
    test::expect_glob("**/*", {"", "foo", "foo/bar", "foo/bar/baz"});
    test::expect_glob("**/x", {"x", "/x", "/x/x/x/x/x", "a/b/c/x"}, {"ax", "a/bx"});
    test::expect_glob("**/*.js",
                      {"foo.js", "/foo.js", "folder/foo.js", "/node_modules/foo.js"},
                      {"foo.jss", "some.js/test", "/some.js/test"});
    test::expect_glob("**/project.json",
                      {"project.json", "/project.json", "some/folder/project.json"},
                      {"some/folder/file_project.json", "some/rrproject.json"});
    test::expect_glob("**/.*",
                      {".git", "/.git", "path/.hidden.txt", "/path/.git"},
                      {"git", "path/git", "pat.h/hidden.txt"});
    test::expect_glob("**/._*", {"._git", "/path/._hidden.txt"}, {"hidden._txt", "path/git"});
}

ZEST_CASE(leading_with_segment_wildcards) {
    test::expect_glob("**/[0-9]*",
                      {"114514foo", "foo/bar/114514", "foo/bar/114514zzz"},
                      {"foo/bar/zzz", "foo/bar/zzz114514"});
    test::expect_glob("**/*[0-9]", {"foo5", "foo/bar/zzz114514"}, {"foo/bar/zzz"});
    test::expect_glob(
        "**/*foo.{c,cpp}",
        {"bar/foo.cpp", "bar/barfoo.cpp", "/foofoo.cpp", "foo/foo/foofoo.cpp", "foo.cpp"});
    test::expect_glob("**/*.{cc,cpp}", {"foo/bar/baz.cc", "foo/foo/foo.cpp", "foo/bar/.cc"});
    test::expect_glob("**/*?.{cc,cpp}", {"a/b/aaa.cc", "a/b/a.cc"}, {"a/b/.cc"});
    test::expect_glob("**/?*.{cc,cpp}", {"a/b/aaa.cc", "a/b/a.cc"}, {"a/b/.cc"});
    test::expect_glob("**/?.js", {"a.js", "foo/b.js", "a/b/c.js"}, {"ab.js", "foo/ab.js", ".js"});
    test::expect_glob("**/?", {"a", "foo/a", "a/b/c/d"}, {"ab", "foo/ab"});
    test::expect_glob("**/a*.cc", {"x/ab.cc", "x/y/a.cc"}, {"x/a/b.cc", "a/b.cc"});
    test::expect_glob("**/?x", {"ax", "aa/ax"}, {"aax"});
}

ZEST_CASE(leading_with_a_literal_path) {
    test::expect_glob("**/include/test/*.{cc,hh,c,h,cpp,hpp}",
                      {"include/test/aaa.cc",
                       "/include/test/aaa.cc",
                       "xxx/yyy/include/test/aaa.cc",
                       "include/foo/bar/baz/include/test/bbb.hh",
                       "include/include/include/include/include/test/bbb.hpp"});
    test::expect_glob("**/a/b", {"a/b", "q/a/b"}, {"aX/b", "q/aX/b"});
    test::expect_glob("**/中/文", {"x/中/文"}, {"x中/文"});
}

ZEST_CASE(trailing_matches_the_directory_and_below) {
    test::expect_glob("x/**", {"x", "x/", "x/foo/bar/baz"}, {"xy"});
    test::expect_glob("test/**",
                      {"test", "test/foo", "test/foo/", "test/other/foo.js"},
                      {"est/other/foo.js"});
    test::expect_glob("/**", {"/foo", "/foo/bar"}, {"foo", "foo/bar"});
}

ZEST_CASE(middle_matches_any_depth_between) {
    test::expect_glob("test/**/*.js",
                      {"test/foo.js", "test/other/foo.js", "test/other/more/foo.js"},
                      {"test/foo.ts", "test/other/more/foo.ts"});
    test::expect_glob("some/**/*.js",
                      {"some/foo.js", "some/folder/foo.js"},
                      {"something/foo.js", "something/folder/foo.js"});
    test::expect_glob("/DNXConsoleApp/**/*.cs",
                      {"/DNXConsoleApp/Program.cs", "/DNXConsoleApp/foo/Program.cs"});
    test::expect_glob("src/**/test[0-9].cpp",
                      {"src/a/b/test1.cpp"},
                      {"src/test1.cpp/child", "src/a/test10.cpp", "other/test1.cpp"});
}

ZEST_CASE(several_globstars) {
    test::expect_glob("**/**/*.js",
                      {"foo.js", "/foo.js", "folder/foo.js"},
                      {"foo.jss", "some.js/test"});
    test::expect_glob("**/node_modules/**/*.js",
                      {"node_modules/foo.js", "/node_modules/some/folder/foo.js"},
                      {"foo.js", "folder/foo.js", "node_modules/some/folder/foo.ts"});
    test::expect_glob("**/foo/**/bar",
                      {"foo/bar", "a/foo/b/bar", "a/b/foo/c/d/e/bar", "/foo/bar"},
                      {"a/b/bar", "foo/baz", "foobar"});
    test::expect_glob("**/a/**/b/**/c",
                      {"a/b/c", "x/a/y/b/z/c", "a/x/a/b/y/b/c"},
                      {"a/c", "a/b", "a/x/a/b/y/b/d"});
    test::expect_glob("**/*/foo", {"a/foo", "x/y/a/foo"}, {"foo"});
    test::expect_glob("**/*.js/**/test",
                      {"foo.js/test", "foo.js/a/b/test", "a/foo.js/test"},
                      {"foo/test"});
}

ZEST_CASE(directory_anywhere) {
    test::expect_glob("**/中/文/**",
                      {"中/文", "x/中/文/y", "x中/文z/中/文/y"},
                      {"x中/文/y", "中/文字/y"});
}

ZEST_CASE(absorbs_one_separator_at_most) {
    test::expect_glob("foo/**", {"foo", "foo/a/b"});
    test::expect_glob("x*/**", {"x"});
    test::expect_glob("foo/**/*", {"foo/a"}, {"foo"});
    test::expect_glob("foo/**/bar", {"foo/bar"}, {"foo"});
    test::expect_glob("x*/**/*", {"x/y"}, {"x"});
    test::expect_glob("*a/**/*", {"aa/b"}, {"aa"});
    test::expect_glob("a/**/**", {"a", "a/b"});
    test::expect_glob("{a/**/**}", {"a", "a/b"});
}

ZEST_CASE(zero_segments_leave_empty_ones_to_stars) {
    test::expect_glob("**/*/*", {"/", "a/b"}, {""});
    test::expect_glob("x/**/*/*", {"x//", "x/a/b"}, {"x/a"});
}

ZEST_CASE(trailing_slash_after_a_globstar) {
    test::expect_glob("**/a/", {"a/", "x/a/", "a/a/", "a/b/a/"}, {"a", "a/b/", "a/a", "a/a/x"});
    test::expect_glob("**/{中,文}/", {"x/中/文/"}, {"x/中/文"});
}

ZEST_CASE(prefix_before_a_globstar_segment_stays_in_its_segment) {
    // Each pattern behaves as its brace-wrapped twin, whose alternative keeps the prefix.
    for(auto pattern: {"a**/?", "{a**/?}"}) {
        test::expect_glob(pattern, {"a/c", "aa/c"}, {"aa", "ab", "a"});
    }
    test::expect_glob("x**/y", {"x/y", "xx/y"}, {"xy"});
    test::expect_glob("a**/[bc]", {"a/b"}, {"ab"});
    test::expect_glob(R"(a**/\?)", {"a/?"}, {"a?"});
}

ZEST_CASE(leading_slash_must_be_matched) {
    test::expect_glob("/*", {"/foo", "/"}, {"foo", "foo/bar", "/foo/bar"});
    test::expect_glob("/**/*.cpp", {"/x/a.cpp"}, {"x/a.cpp"});
}

ZEST_CASE(retries_after_a_near_miss) {
    // The first candidate matches as far as `b` but fails at `/x`.
    const std::string as(32, 'a');
    test::expect_glob("**/*a*a*a*ab/x", {as + "b/y/aaaab/x"}, {as + "b/y/aaaab/y"});
    test::expect_glob("**/*a*a/**/b/x", {"aaaa/other/b/x"}, {"aaaa/other/b/y"});
    test::expect_glob("**/a/*b/c", {"a/a/b/c"});
    test::expect_glob("**/a*b", {std::string(1000, 'a') + "/ab"}, {std::string(1000, 'a') + "/ac"});
    test::expect_glob("**/a/", {"a/a/"}, {"a/a/x"});
}

ZEST_CASE(retries_through_long_directories) {
    const std::string directory(70000, 'a');
    test::expect_glob("**/目标.cpp", {directory + "/目标.cpp"}, {directory + "目标.cpp"});
    test::expect_glob("**/*a*/b", {directory + "/c/a/b"}, {directory + "/c/a/c"});
    const std::string segment = std::string(48, 'a') + '/';
    test::expect_glob("**/*a*/*a*/*a*/c",
                      {test::repeat(segment, 3) + "y/a/a/a/c"},
                      {test::repeat(segment, 3) + "y/a/a/a/d"});
}

ZEST_CASE(many_segments_without_a_retry_cutoff) {
    const std::string pattern = "**/" + test::repeat("*a/", 60) + "Z";
    for(int width: {20, 80}) {
        for(int count: {80, 160}) {
            ZEST_CONTEXT("width {}, count {}", width, count);
            const std::string input = test::repeat(std::string(width, 'b') + "a/", count);
            test::expect_glob(pattern, {input + "Z"}, {input + "Y"});
        }
    }
}

ZEST_CASE(more_globstars_than_a_small_stack) {
    const std::string pattern = test::repeat("**/a/", 24) + "目标";
    const std::string input = test::repeat("x/a/", 24);
    test::expect_glob(pattern, {input + "目标"}, {input + "错"});
}

};  // ZEST_SUITE(support_glob_pattern_globstar)

}  // namespace

}  // namespace kota
