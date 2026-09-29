#include "support/harness/glob.h"
#include "kota/zest/zest.h"

namespace kota {

namespace {

// A pattern with braces expands, textually, into one arm for each choice of their terms; a
// path matches when any arm does.

ZEST_SUITE(support_glob_pattern_brace) {

ZEST_CASE(terms_of_whole_names) {
    test::expect_glob("{node_modules,testing}",
                      {"node_modules", "testing"},
                      {"node_module", "dtesting"});
    test::expect_glob("{AAA,BBB,AB*}", {"AAA", "BBB", "AB", "ABCD"}, {"CCC"});
}

ZEST_CASE(terms_of_extensions) {
    test::expect_glob("*.{html,js}",
                      {"foo.js", "foo.html"},
                      {"folder/foo.js", "/node_modules/foo.js", "foo.jss", "some.js/test"});
    test::expect_glob("*.{html}", {"foo.html"}, {"foo.js"});
}

ZEST_CASE(terms_within_a_path) {
    test::expect_glob("proj/{build*,include,src}/*.{cc,cpp,h,hpp}",
                      {"proj/include/foo.cc",
                       "proj/include/bar.cpp",
                       "proj/build-yyy/foo.h",
                       "proj/build/foo.cpp"},
                      {"proj/include/xxx/yyy/zzz/foo.cc", "proj/build-xxx/xxx/yyy/zzz/foo.cpp"});
}

ZEST_CASE(several_braces_multiply) {
    test::expect_glob("{a,b}.{c,d}", {"a.c", "a.d", "b.c", "b.d"}, {"a.e", "c.a"});
}

ZEST_CASE(terms_with_globstars) {
    test::expect_glob("**/{foo,bar}",
                      {"foo", "bar", "test/foo", "other/more/bar", "/foo", "/other/more/bar"},
                      {"baz", "foox"});
    test::expect_glob("{foo,bar}/**",
                      {"foo", "bar", "bar/", "foo/test", "bar/other/more/"},
                      {"baz/test"});
    test::expect_glob(
        "{**/*.d.ts,**/*.js}",
        {"foo.js", "testing/foo.js", "/testing/foo.js", "foo.d.ts", "/testing/foo.d.ts"},
        {"foo.d", "testing/foo.d"});
    test::expect_glob("{**/*.d.ts,**/*.js,path/simple.jgs}",
                      {"foo.js", "path/simple.jgs"},
                      {"/path/simple.jgs"});
    test::expect_glob("{**/package.json,**/project.json}",
                      {"package.json", "/package.json"},
                      {"xpackage.json", "/xpackage.json"});
}

ZEST_CASE(terms_after_a_prefix) {
    test::expect_glob("prefix/{**/*.d.ts,**/*.js,foo.[0-9]}",
                      {"prefix/foo.5", "prefix/foo.8", "prefix/foo.js"},
                      {"prefix/bar.5", "prefix/foo.f"});
    test::expect_glob("{**/*.d.ts,**/*.js,foo.[0-9]}", {"foo.5", "foo.js"}, {"bar.5", "foo.f"});
    // With a prefix before it, `*` means `a*`: still within the segment.
    test::expect_glob("a{*,foo}", {"ab", "afoo"}, {"a/b"});
}

ZEST_CASE(empty_terms_match_the_empty_text) {
    test::expect_glob("{,a}", {"", "a"}, {"b"});
    test::expect_glob("foo/{,x}", {"foo/", "foo/x"}, {"foo"});
    test::expect_glob("{,src}/**", {"", "/foo"}, {"foo"});
}

ZEST_CASE(escaped_comma_is_part_of_a_term) {
    test::expect_glob(R"({a\,b,c})", {"a,b", "c"}, {"a", "b"});
}

ZEST_CASE(brackets_inside_terms) {
    test::expect_glob("{[a-z]oo,[0-9]ar}", {"foo", "boo", "1ar", "9ar"}, {"Foo", "bar"});
    test::expect_glob(R"({foo.[\*\?],bar})", {"foo.*", "foo.?", "bar"}, {"foo.x"});
}

ZEST_CASE(star_term_stays_in_its_segment) {
    test::expect_glob("{*,foo}", {"foo", "bar"}, {"a/b", "/foo"});
    test::expect_glob("{test_*.cpp,foo**foo}", {"test_.cpp", "foofoo"}, {"foo/foo"});
}

ZEST_CASE(globstar_term_matches_everything) {
    test::expect_glob("{foo,**}", {"a/b/c", "", "foo"});
    test::expect_glob("foo/{**,x}", {"foo", "foo/x", "foo/a/b"}, {"bar"});
}

ZEST_CASE(terms_before_a_globstar) {
    test::expect_glob("{src,include}/**",
                      {"src", "src/", "src/中文.cpp", "include/a/b"},
                      {"", "/src", "srcx/a", "x/include/a"});
    test::expect_glob("{**/node_modules/**,**/.git/**,**/bower_components/**}",
                      {"node_modules",
                       "/node_modules/more",
                       "some/test/node_modules",
                       "bower_components/more",
                       "/some/test/.git"},
                      {"tempting", "/some/test/tempting"});
}

};  // ZEST_SUITE(support_glob_pattern_brace)

}  // namespace

}  // namespace kota
