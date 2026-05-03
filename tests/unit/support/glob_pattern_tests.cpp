#include "kota/zest/zest.h"
#include "kota/support/glob_pattern.h"

namespace kota {

namespace {

#define PATDEF(name, pat_str)                                                                      \
    auto _res_##name = kota::GlobPattern::create(pat_str, 100);                                    \
    EXPECT_TRUE(_res_##name.has_value());                                                          \
    if(!_res_##name.has_value())                                                                   \
        return;                                                                                    \
    auto name = std::move(*_res_##name);

TEST_SUITE(glob_pattern) {

TEST_CASE(pattern_sema) {
    auto pat1 = kota::GlobPattern::create("**/****.{c,cc}", 100);
    EXPECT_FALSE(pat1.has_value());

    auto pat2 = kota::GlobPattern::create("/foo/bar/baz////aaa.{c,cc}", 100);
    EXPECT_FALSE(pat2.has_value());

    auto pat3 = kota::GlobPattern::create("/foo/bar/baz/**////*.{c,cc}", 100);
    EXPECT_FALSE(pat3.has_value());
}

TEST_CASE(max_sub_glob) {
    auto pat1 = kota::GlobPattern::create("{AAA,BBB,AB*}");
    EXPECT_TRUE(pat1.has_value());
    EXPECT_TRUE(pat1->match("AAA"));
    EXPECT_TRUE(pat1->match("BBB"));
    EXPECT_TRUE(pat1->match("AB"));
    EXPECT_TRUE(pat1->match("ABCD"));
    EXPECT_FALSE(pat1->match("CCC"));
    EXPECT_TRUE(pat1->match("ABCDE"));
}

TEST_CASE(simple) {
    PATDEF(pat1, "node_modules")
    EXPECT_TRUE(pat1.match("node_modules"));
    EXPECT_FALSE(pat1.match("node_module"));
    EXPECT_FALSE(pat1.match("/node_modules"));
    EXPECT_FALSE(pat1.match("test/node_modules"));

    PATDEF(pat2, "test.txt")
    EXPECT_TRUE(pat2.match("test.txt"));
    EXPECT_FALSE(pat2.match("test?txt"));
    EXPECT_FALSE(pat2.match("/text.txt"));
    EXPECT_FALSE(pat2.match("test/test.txt"));

    PATDEF(pat3, "test(.txt")
    EXPECT_TRUE(pat3.match("test(.txt"));
    EXPECT_FALSE(pat3.match("test?txt"));

    PATDEF(pat4, "qunit")
    EXPECT_TRUE(pat4.match("qunit"));
    EXPECT_FALSE(pat4.match("qunit.css"));
    EXPECT_FALSE(pat4.match("test/qunit"));

    PATDEF(pat5, "/DNXConsoleApp/**/*.cs")
    EXPECT_TRUE(pat5.match("/DNXConsoleApp/Program.cs"));
    EXPECT_TRUE(pat5.match("/DNXConsoleApp/foo/Program.cs"));
}

TEST_CASE(dot_hidden) {
    PATDEF(pat1, ".*")
    EXPECT_TRUE(pat1.match(".git"));
    EXPECT_TRUE(pat1.match(".hidden.txt"));
    EXPECT_FALSE(pat1.match("git"));
    EXPECT_FALSE(pat1.match("hidden.txt"));
    EXPECT_FALSE(pat1.match("path/.git"));
    EXPECT_FALSE(pat1.match("path/.hidden.txt"));

    PATDEF(pat2, "**/.*")
    EXPECT_TRUE(pat2.match(".git"));
    EXPECT_TRUE(pat2.match("/.git"));
    EXPECT_TRUE(pat2.match(".hidden.txt"));
    EXPECT_FALSE(pat2.match("git"));
    EXPECT_FALSE(pat2.match("hidden.txt"));
    EXPECT_TRUE(pat2.match("path/.git"));
    EXPECT_TRUE(pat2.match("path/.hidden.txt"));
    EXPECT_TRUE(pat2.match("/path/.git"));
    EXPECT_TRUE(pat2.match("/path/.hidden.txt"));
    EXPECT_FALSE(pat2.match("path/git"));
    EXPECT_FALSE(pat2.match("pat.h/hidden.txt"));

    PATDEF(pat3, "._*")
    EXPECT_TRUE(pat3.match("._git"));
    EXPECT_TRUE(pat3.match("._hidden.txt"));
    EXPECT_FALSE(pat3.match("git"));
    EXPECT_FALSE(pat3.match("hidden.txt"));
    EXPECT_FALSE(pat3.match("path/._git"));
    EXPECT_FALSE(pat3.match("path/._hidden.txt"));

    PATDEF(pat4, "**/._*")
    EXPECT_TRUE(pat4.match("._git"));
    EXPECT_TRUE(pat4.match("._hidden.txt"));
    EXPECT_FALSE(pat4.match("git"));
    EXPECT_FALSE(pat4.match("hidden._txt"));
    EXPECT_TRUE(pat4.match("path/._git"));
    EXPECT_TRUE(pat4.match("path/._hidden.txt"));
    EXPECT_TRUE(pat4.match("/path/._git"));
    EXPECT_TRUE(pat4.match("/path/._hidden.txt"));
    EXPECT_FALSE(pat4.match("path/git"));
    EXPECT_FALSE(pat4.match("pat.h/hidden._txt"));
}

TEST_CASE(escape_character) {
    PATDEF(pat1, R"(\*star)")
    EXPECT_TRUE(pat1.match("*star"));

    PATDEF(pat2, R"(\{\*\})")
    EXPECT_TRUE(pat2.match("{*}"));
}

TEST_CASE(bracket_expr) {
    PATDEF(pat1, R"([a-zA-Z\]])")
    EXPECT_TRUE(pat1.match(R"(])"));
    EXPECT_FALSE(pat1.match(R"([)"));
    EXPECT_TRUE(pat1.match(R"(s)"));
    EXPECT_TRUE(pat1.match(R"(S)"));
    EXPECT_FALSE(pat1.match(R"(0)"));

    PATDEF(pat2, R"([\\^a-zA-Z""\\])")
    EXPECT_TRUE(pat2.match(R"(")"));
    EXPECT_TRUE(pat2.match(R"(^)"));
    EXPECT_TRUE(pat2.match(R"(\)"));
    EXPECT_TRUE(pat2.match(R"(")"));
    EXPECT_TRUE(pat2.match(R"(x)"));
    EXPECT_TRUE(pat2.match(R"(X)"));
    EXPECT_FALSE(pat2.match(R"(0)"));

    PATDEF(pat3, R"([!0-9a-fA-F\-+\*])")
    EXPECT_FALSE(pat3.match("1"));
    EXPECT_FALSE(pat3.match("*"));
    EXPECT_TRUE(pat3.match("s"));
    EXPECT_TRUE(pat3.match("S"));
    EXPECT_TRUE(pat3.match("H"));
    EXPECT_TRUE(pat3.match("]"));

    PATDEF(pat4, R"([^\^0-9a-fA-F\-+\*])")
    EXPECT_FALSE(pat4.match("1"));
    EXPECT_FALSE(pat4.match("*"));
    EXPECT_FALSE(pat4.match("^"));
    EXPECT_TRUE(pat4.match("s"));
    EXPECT_TRUE(pat4.match("S"));
    EXPECT_TRUE(pat4.match("H"));
    EXPECT_TRUE(pat4.match("]"));

    PATDEF(pat5, R"([\*-\^])")
    EXPECT_TRUE(pat5.match("*"));
    EXPECT_FALSE(pat5.match("a"));
    EXPECT_FALSE(pat5.match("z"));
    EXPECT_TRUE(pat5.match("A"));
    EXPECT_TRUE(pat5.match("Z"));
    EXPECT_TRUE(pat5.match("\\"));
    EXPECT_TRUE(pat5.match("^"));
    EXPECT_TRUE(pat5.match("-"));

    PATDEF(pat6, "foo.[^0-9]")
    EXPECT_FALSE(pat6.match("foo.5"));
    EXPECT_FALSE(pat6.match("foo.8"));
    EXPECT_FALSE(pat6.match("bar.5"));
    EXPECT_TRUE(pat6.match("foo.f"));

    PATDEF(pat7, "foo.[!0-9]")
    EXPECT_FALSE(pat7.match("foo.5"));
    EXPECT_FALSE(pat7.match("foo.8"));
    EXPECT_FALSE(pat7.match("bar.5"));
    EXPECT_TRUE(pat7.match("foo.f"));

    PATDEF(pat8, "foo.[0!^*?]")
    EXPECT_FALSE(pat8.match("foo.5"));
    EXPECT_FALSE(pat8.match("foo.8"));
    EXPECT_TRUE(pat8.match("foo.0"));
    EXPECT_TRUE(pat8.match("foo.!"));
    EXPECT_TRUE(pat8.match("foo.^"));
    EXPECT_TRUE(pat8.match("foo.*"));
    EXPECT_TRUE(pat8.match("foo.?"));

    PATDEF(pat9, "foo[/]bar")
    EXPECT_FALSE(pat9.match("foo/bar"));

    PATDEF(pat10, "foo.[[]")
    EXPECT_TRUE(pat10.match("foo.["));

    PATDEF(pat11, "foo.[]]")
    EXPECT_TRUE(pat11.match("foo.]"));

    PATDEF(pat12, "foo.[][!]")
    EXPECT_TRUE(pat12.match("foo.]"));
    EXPECT_TRUE(pat12.match("foo.["));
    EXPECT_TRUE(pat12.match("foo.!"));

    PATDEF(pat13, "foo.[]-]")
    EXPECT_TRUE(pat13.match("foo.]"));
    EXPECT_TRUE(pat13.match("foo.-"));

    PATDEF(pat14, "foo.[0-9]")
    EXPECT_TRUE(pat14.match("foo.5"));
    EXPECT_TRUE(pat14.match("foo.8"));
    EXPECT_FALSE(pat14.match("bar.5"));
    EXPECT_FALSE(pat14.match("foo.f"));
}

TEST_CASE(brace_expr) {
    PATDEF(pat1, "*foo[0-9a-z].{c,cpp,cppm,?pp}")
    EXPECT_FALSE(pat1.match("foo1.cc"));
    EXPECT_TRUE(pat1.match("foo2.cpp"));
    EXPECT_TRUE(pat1.match("foo3.cppm"));
    EXPECT_TRUE(pat1.match("foot.cppm"));
    EXPECT_TRUE(pat1.match("foot.hpp"));
    EXPECT_TRUE(pat1.match("foot.app"));
    EXPECT_FALSE(pat1.match("fooD.cppm"));
    EXPECT_FALSE(pat1.match("BarfooD.cppm"));
    EXPECT_FALSE(pat1.match("foofooD.cppm"));

    PATDEF(pat2, "proj/{build*,include,src}/*.{cc,cpp,h,hpp}")
    EXPECT_TRUE(pat2.match("proj/include/foo.cc"));
    EXPECT_TRUE(pat2.match("proj/include/bar.cpp"));
    EXPECT_FALSE(pat2.match("proj/include/xxx/yyy/zzz/foo.cc"));
    EXPECT_TRUE(pat2.match("proj/build-yyy/foo.h"));
    EXPECT_TRUE(pat2.match("proj/build-xxx/foo.cpp"));
    EXPECT_TRUE(pat2.match("proj/build/foo.cpp"));
    EXPECT_FALSE(pat2.match("proj/build-xxx/xxx/yyy/zzz/foo.cpp"));

    PATDEF(pat3, "*.{html,js}")
    EXPECT_TRUE(pat3.match("foo.js"));
    EXPECT_TRUE(pat3.match("foo.html"));
    EXPECT_FALSE(pat3.match("folder/foo.js"));
    EXPECT_FALSE(pat3.match("/node_modules/foo.js"));
    EXPECT_FALSE(pat3.match("foo.jss"));
    EXPECT_FALSE(pat3.match("some.js/test"));

    PATDEF(pat4, "*.{html}")
    EXPECT_TRUE(pat4.match("foo.html"));
    EXPECT_FALSE(pat4.match("foo.js"));
    EXPECT_FALSE(pat4.match("folder/foo.js"));
    EXPECT_FALSE(pat4.match("/node_modules/foo.js"));
    EXPECT_FALSE(pat4.match("foo.jss"));
    EXPECT_FALSE(pat4.match("some.js/test"));

    PATDEF(pat5, "{node_modules,testing}")
    EXPECT_TRUE(pat5.match("node_modules"));
    EXPECT_TRUE(pat5.match("testing"));
    EXPECT_FALSE(pat5.match("node_module"));
    EXPECT_FALSE(pat5.match("dtesting"));

    PATDEF(pat6, "**/{foo,bar}")
    EXPECT_TRUE(pat6.match("foo"));
    EXPECT_TRUE(pat6.match("bar"));
    EXPECT_TRUE(pat6.match("test/foo"));
    EXPECT_TRUE(pat6.match("test/bar"));
    EXPECT_TRUE(pat6.match("other/more/foo"));
    EXPECT_TRUE(pat6.match("other/more/bar"));
    EXPECT_TRUE(pat6.match("/foo"));
    EXPECT_TRUE(pat6.match("/bar"));
    EXPECT_TRUE(pat6.match("/test/foo"));
    EXPECT_TRUE(pat6.match("/test/bar"));
    EXPECT_TRUE(pat6.match("/other/more/foo"));
    EXPECT_TRUE(pat6.match("/other/more/bar"));

    PATDEF(pat7, "{foo,bar}/**")
    EXPECT_TRUE(pat7.match("foo"));
    EXPECT_TRUE(pat7.match("bar"));
    EXPECT_TRUE(pat7.match("bar/"));
    EXPECT_TRUE(pat7.match("foo/test"));
    EXPECT_TRUE(pat7.match("bar/test"));
    EXPECT_TRUE(pat7.match("bar/test/"));
    EXPECT_TRUE(pat7.match("foo/other/more"));
    EXPECT_TRUE(pat7.match("bar/other/more"));
    EXPECT_TRUE(pat7.match("bar/other/more/"));

    PATDEF(pat8, "{**/*.d.ts,**/*.js}")
    EXPECT_TRUE(pat8.match("foo.js"));
    EXPECT_TRUE(pat8.match("testing/foo.js"));
    EXPECT_TRUE(pat8.match("/testing/foo.js"));
    EXPECT_TRUE(pat8.match("foo.d.ts"));
    EXPECT_TRUE(pat8.match("testing/foo.d.ts"));
    EXPECT_TRUE(pat8.match("/testing/foo.d.ts"));
    EXPECT_FALSE(pat8.match("foo.d"));
    EXPECT_FALSE(pat8.match("testing/foo.d"));
    EXPECT_FALSE(pat8.match("/testing/foo.d"));

    PATDEF(pat9, "{**/*.d.ts,**/*.js,path/simple.jgs}")
    EXPECT_TRUE(pat9.match("foo.js"));
    EXPECT_TRUE(pat9.match("testing/foo.js"));
    EXPECT_TRUE(pat9.match("/testing/foo.js"));
    EXPECT_TRUE(pat9.match("path/simple.jgs"));
    EXPECT_FALSE(pat9.match("/path/simple.jgs"));

    PATDEF(pat10, "{**/*.d.ts,**/*.js,foo.[0-9]}")
    EXPECT_TRUE(pat10.match("foo.5"));
    EXPECT_TRUE(pat10.match("foo.8"));
    EXPECT_FALSE(pat10.match("bar.5"));
    EXPECT_FALSE(pat10.match("foo.f"));
    EXPECT_TRUE(pat10.match("foo.js"));

    PATDEF(pat11, "prefix/{**/*.d.ts,**/*.js,foo.[0-9]}")
    EXPECT_TRUE(pat11.match("prefix/foo.5"));
    EXPECT_TRUE(pat11.match("prefix/foo.8"));
    EXPECT_FALSE(pat11.match("prefix/bar.5"));
    EXPECT_FALSE(pat11.match("prefix/foo.f"));
    EXPECT_TRUE(pat11.match("prefix/foo.js"));
}

TEST_CASE(globstar_prefix) {
    // **/* — match any path
    PATDEF(pat1, "**/*")
    EXPECT_TRUE(pat1.match("foo"));
    EXPECT_TRUE(pat1.match("foo/bar"));
    EXPECT_TRUE(pat1.match("foo/bar/baz"));

    // **/[0-9]* — last segment starts with digit
    PATDEF(pat2, "**/[0-9]*")
    EXPECT_TRUE(pat2.match("114514foo"));
    EXPECT_FALSE(pat2.match("foo/bar/baz/xxx/yyy/zzz"));
    EXPECT_FALSE(pat2.match("foo/bar/baz/xxx/yyy/zzz114514"));
    EXPECT_TRUE(pat2.match("foo/bar/baz/xxx/yyy/114514"));
    EXPECT_TRUE(pat2.match("foo/bar/baz/xxx/yyy/114514zzz"));

    // **/*[0-9] — last segment ends with digit
    PATDEF(pat3, "**/*[0-9]")
    EXPECT_TRUE(pat3.match("foo5"));
    EXPECT_FALSE(pat3.match("foo/bar/baz/xxx/yyy/zzz"));
    EXPECT_TRUE(pat3.match("foo/bar/baz/xxx/yyy/zzz114514"));

    // **/include/test/*.{cc,...} — globstar prefix with multi-segment literal
    PATDEF(pat4, "**/include/test/*.{cc,hh,c,h,cpp,hpp}")
    EXPECT_TRUE(pat4.match("include/test/aaa.cc"));
    EXPECT_TRUE(pat4.match("/include/test/aaa.cc"));
    EXPECT_TRUE(pat4.match("xxx/yyy/include/test/aaa.cc"));
    EXPECT_TRUE(pat4.match("include/foo/bar/baz/include/test/bbb.hh"));
    EXPECT_TRUE(pat4.match("include/include/include/include/include/test/bbb.hpp"));

    // **include/test/*.{cc,...} — globstar attached to literal (no slash after **)
    PATDEF(pat5, "**include/test/*.{cc,hh,c,h,cpp,hpp}")
    EXPECT_TRUE(pat5.match("include/test/fff.hpp"));
    EXPECT_TRUE(pat5.match("xxx-yyy-include/test/fff.hpp"));
    EXPECT_TRUE(pat5.match("xxx-yyy-include/test/.hpp"));
    EXPECT_TRUE(pat5.match("/include/test/aaa.cc"));
    EXPECT_TRUE(pat5.match("include/foo/bar/baz/include/test/bbb.hh"));

    // **/*foo.{c,cpp} — globstar prefix with wildcard suffix
    PATDEF(pat6, "**/*foo.{c,cpp}")
    EXPECT_TRUE(pat6.match("bar/foo.cpp"));
    EXPECT_TRUE(pat6.match("bar/barfoo.cpp"));
    EXPECT_TRUE(pat6.match("/foofoo.cpp"));
    EXPECT_TRUE(pat6.match("foo/foo/foo/foo/foofoo.cpp"));
    EXPECT_TRUE(pat6.match("foofoo.cpp"));
    EXPECT_TRUE(pat6.match("barfoo.cpp"));
    EXPECT_TRUE(pat6.match("foo.cpp"));

    // ** — matches everything
    PATDEF(pat7, "**")
    EXPECT_TRUE(pat7.match("foo"));
    EXPECT_TRUE(pat7.match("foo/bar/baz"));
    EXPECT_TRUE(pat7.match("/"));
    EXPECT_TRUE(pat7.match("foo.js"));
    EXPECT_TRUE(pat7.match("folder/foo.js"));
    EXPECT_TRUE(pat7.match("folder/foo/"));
    EXPECT_TRUE(pat7.match("/node_modules/foo.js"));
    EXPECT_TRUE(pat7.match("foo.jss"));
    EXPECT_TRUE(pat7.match("some.js/test"));

    // **/x — match literal at any depth
    PATDEF(pat8, "**/x")
    EXPECT_TRUE(pat8.match("x"));
    EXPECT_TRUE(pat8.match("/x"));
    EXPECT_TRUE(pat8.match("/x/x/x/x/x"));

    // **/*.{cc,cpp} — extension match at any depth
    PATDEF(pat9, "**/*.{cc,cpp}")
    EXPECT_TRUE(pat9.match("foo/bar/baz.cc"));
    EXPECT_TRUE(pat9.match("foo/foo/foo.cpp"));
    EXPECT_TRUE(pat9.match("foo/bar/.cc"));

    // **/*?.{cc,cpp} — wildcard then question before extension
    PATDEF(pat10, "**/*?.{cc,cpp}")
    EXPECT_TRUE(pat10.match("foo/bar/baz/xxx/yyy/zzz/aaa.cc"));
    EXPECT_TRUE(pat10.match("foo/bar/baz/xxx/yyy/zzz/a.cc"));
    EXPECT_FALSE(pat10.match("foo/bar/baz/xxx/yyy/zzz/.cc"));

    // **/?*.{cc,cpp} — question then wildcard before extension
    // After ?* special case removal, ? matches one char and * matches rest independently.
    // With ** backtracking, the * can hop across / boundaries, so .cc after a / is matched
    // when ** absorbs enough of the prefix.
    PATDEF(pat11, "**/?*.{cc,cpp}")
    EXPECT_TRUE(pat11.match("foo/bar/baz/xxx/yyy/zzz/aaa.cc"));
    EXPECT_TRUE(pat11.match("foo/bar/baz/xxx/yyy/zzz/a.cc"));
    EXPECT_TRUE(pat11.match("foo/bar/baz/xxx/yyy/zzz/.cc"));

    // **/*.js — JS file at any depth
    PATDEF(pat12, "**/*.js")
    EXPECT_TRUE(pat12.match("foo.js"));
    EXPECT_TRUE(pat12.match("/foo.js"));
    EXPECT_TRUE(pat12.match("folder/foo.js"));
    EXPECT_TRUE(pat12.match("/node_modules/foo.js"));
    EXPECT_FALSE(pat12.match("foo.jss"));
    EXPECT_FALSE(pat12.match("some.js/test"));
    EXPECT_FALSE(pat12.match("/some.js/test"));

    // **/project.json — exact filename at any depth
    PATDEF(pat13, "**/project.json")
    EXPECT_TRUE(pat13.match("project.json"));
    EXPECT_TRUE(pat13.match("/project.json"));
    EXPECT_TRUE(pat13.match("some/folder/project.json"));
    EXPECT_TRUE(pat13.match("/some/folder/project.json"));
    EXPECT_FALSE(pat13.match("some/folder/file_project.json"));
    EXPECT_FALSE(pat13.match("some/folder/fileproject.json"));
    EXPECT_FALSE(pat13.match("some/rrproject.json"));
}

TEST_CASE(globstar_suffix) {
    // x/** — everything under x/
    PATDEF(pat1, "x/**")
    EXPECT_TRUE(pat1.match("x/"));
    EXPECT_TRUE(pat1.match("x/foo/bar/baz"));
    EXPECT_TRUE(pat1.match("x"));

    // test/** — everything under test/
    PATDEF(pat2, "test/**")
    EXPECT_TRUE(pat2.match("test"));
    EXPECT_TRUE(pat2.match("test/foo"));
    EXPECT_TRUE(pat2.match("test/foo/"));
    EXPECT_TRUE(pat2.match("test/foo.js"));
    EXPECT_TRUE(pat2.match("test/other/foo.js"));
    EXPECT_FALSE(pat2.match("est/other/foo.js"));
}

TEST_CASE(globstar_middle) {
    // test/**/*.js — JS files under test/ at any depth
    PATDEF(pat1, "test/**/*.js")
    EXPECT_TRUE(pat1.match("test/foo.js"));
    EXPECT_TRUE(pat1.match("test/other/foo.js"));
    EXPECT_TRUE(pat1.match("test/other/more/foo.js"));
    EXPECT_FALSE(pat1.match("test/foo.ts"));
    EXPECT_FALSE(pat1.match("test/other/foo.ts"));
    EXPECT_FALSE(pat1.match("test/other/more/foo.ts"));

    // some/**/*.js — JS files under some/ at any depth
    PATDEF(pat2, "some/**/*.js")
    EXPECT_TRUE(pat2.match("some/foo.js"));
    EXPECT_TRUE(pat2.match("some/folder/foo.js"));
    EXPECT_FALSE(pat2.match("something/foo.js"));
    EXPECT_FALSE(pat2.match("something/folder/foo.js"));

    // some/**/* — any file under some/ at any depth
    PATDEF(pat3, "some/**/*")
    EXPECT_TRUE(pat3.match("some/foo.js"));
    EXPECT_TRUE(pat3.match("some/folder/foo.js"));
    EXPECT_FALSE(pat3.match("something/foo.js"));
    EXPECT_FALSE(pat3.match("something/folder/foo.js"));
}

TEST_CASE(globstar_complex) {
    // **/**/*.js — double globstar
    PATDEF(pat1, "**/**/*.js")
    EXPECT_TRUE(pat1.match("foo.js"));
    EXPECT_TRUE(pat1.match("/foo.js"));
    EXPECT_TRUE(pat1.match("folder/foo.js"));
    EXPECT_TRUE(pat1.match("/node_modules/foo.js"));
    EXPECT_FALSE(pat1.match("foo.jss"));
    EXPECT_FALSE(pat1.match("some.js/test"));

    // **/node_modules/**/*.js — scoped to node_modules
    PATDEF(pat2, "**/node_modules/**/*.js")
    EXPECT_FALSE(pat2.match("foo.js"));
    EXPECT_FALSE(pat2.match("folder/foo.js"));
    EXPECT_TRUE(pat2.match("node_modules/foo.js"));
    EXPECT_TRUE(pat2.match("/node_modules/foo.js"));
    EXPECT_TRUE(pat2.match("node_modules/some/folder/foo.js"));
    EXPECT_TRUE(pat2.match("/node_modules/some/folder/foo.js"));
    EXPECT_FALSE(pat2.match("node_modules/some/folder/foo.ts"));
    EXPECT_FALSE(pat2.match("foo.jss"));
    EXPECT_FALSE(pat2.match("some.js/test"));

    // Brace with multiple globstar patterns
    PATDEF(pat3, "{**/node_modules/**,**/.git/**,**/bower_components/**}")
    EXPECT_TRUE(pat3.match("node_modules"));
    EXPECT_TRUE(pat3.match("/node_modules"));
    EXPECT_TRUE(pat3.match("/node_modules/more"));
    EXPECT_TRUE(pat3.match("some/test/node_modules"));
    EXPECT_TRUE(pat3.match("/some/test/node_modules"));
    EXPECT_TRUE(pat3.match("bower_components"));
    EXPECT_TRUE(pat3.match("bower_components/more"));
    EXPECT_TRUE(pat3.match("/bower_components"));
    EXPECT_TRUE(pat3.match("some/test/bower_components"));
    EXPECT_TRUE(pat3.match("/some/test/bower_components"));
    EXPECT_TRUE(pat3.match(".git"));
    EXPECT_TRUE(pat3.match("/.git"));
    EXPECT_TRUE(pat3.match("some/test/.git"));
    EXPECT_TRUE(pat3.match("/some/test/.git"));
    EXPECT_FALSE(pat3.match("tempting"));
    EXPECT_FALSE(pat3.match("/tempting"));
    EXPECT_FALSE(pat3.match("some/test/tempting"));
    EXPECT_FALSE(pat3.match("/some/test/tempting"));

    // Brace with multiple globstar-prefixed patterns
    PATDEF(pat4, "{**/package.json,**/project.json}")
    EXPECT_TRUE(pat4.match("package.json"));
    EXPECT_TRUE(pat4.match("/package.json"));
    EXPECT_FALSE(pat4.match("xpackage.json"));
    EXPECT_FALSE(pat4.match("/xpackage.json"));
}

TEST_CASE(error_paths) {
    // Unmatched '['
    auto e1 = kota::GlobPattern::create("foo.[a-z");
    EXPECT_FALSE(e1.has_value());

    // '[' as last character
    auto e2 = kota::GlobPattern::create("{a,[}");
    EXPECT_FALSE(e2.has_value());

    // Stray '\' at end of pattern (in SubGlobPattern)
    auto e3 = kota::GlobPattern::create("foo\\");
    EXPECT_FALSE(e3.has_value());

    // Stray '\' at end inside brace
    auto e4 = kota::GlobPattern::create("{foo\\}");
    EXPECT_FALSE(e4.has_value());

    // Stray '\' inside bracket inside brace
    auto e5 = kota::GlobPattern::create("{[abc\\]}");
    EXPECT_FALSE(e5.has_value());

    // Empty brace expression {}
    auto e6 = kota::GlobPattern::create("foo.{}");
    EXPECT_FALSE(e6.has_value());

    // Nested braces
    auto e7 = kota::GlobPattern::create("{a,{b,c}}");
    EXPECT_FALSE(e7.has_value());

    // Incomplete brace expansion (unmatched '{')
    auto e8 = kota::GlobPattern::create("{foo,bar");
    EXPECT_FALSE(e8.has_value());

    // *** (triple star)
    auto e9 = kota::GlobPattern::create("***.js");
    EXPECT_FALSE(e9.has_value());

    // ** is valid (boundary)
    auto e10 = kota::GlobPattern::create("**.js");
    EXPECT_TRUE(e10.has_value());

    // Multiple consecutive slashes in literal pattern
    auto e11 = kota::GlobPattern::create("foo//bar");
    EXPECT_FALSE(e11.has_value());

    // Multiple consecutive slashes at start
    auto e12 = kota::GlobPattern::create("//foo");
    EXPECT_FALSE(e12.has_value());

    // Multiple consecutive slashes in glob pattern (detected by SubGlobPattern)
    auto e13 = kota::GlobPattern::create("**/foo//*.cc");
    EXPECT_FALSE(e13.has_value());

    // Unmatched '[' in SubGlobPattern
    auto e14 = kota::GlobPattern::create("*[");
    EXPECT_FALSE(e14.has_value());

    // '\' at end inside bracket inside brace
    auto e15 = kota::GlobPattern::create("{[\\]}");
    EXPECT_FALSE(e15.has_value());

    // Range start > end
    auto e16 = kota::GlobPattern::create("[z-a]");
    EXPECT_FALSE(e16.has_value());

    // Range end is stray backslash
    auto e17 = kota::GlobPattern::create("[a-\\]");
    EXPECT_FALSE(e17.has_value());
}

TEST_CASE(empty_and_trivial) {
    // Empty pattern matches only empty string
    PATDEF(pat1, "")
    EXPECT_TRUE(pat1.match(""));
    EXPECT_FALSE(pat1.match("foo"));
    EXPECT_FALSE(pat1.match("/"));

    // Single character pattern
    PATDEF(pat2, "a")
    EXPECT_TRUE(pat2.match("a"));
    EXPECT_FALSE(pat2.match("b"));
    EXPECT_FALSE(pat2.match("ab"));
    EXPECT_FALSE(pat2.match(""));

    // Slash-only pattern
    PATDEF(pat3, "/")
    EXPECT_TRUE(pat3.match("/"));
    EXPECT_FALSE(pat3.match(""));
    EXPECT_FALSE(pat3.match("//"));

    // Literal path with slashes (was rejected before bug fix)
    PATDEF(pat4, "foo/bar")
    EXPECT_TRUE(pat4.match("foo/bar"));
    EXPECT_FALSE(pat4.match("foo/baz"));
    EXPECT_FALSE(pat4.match("foo/bar/baz"));
    EXPECT_FALSE(pat4.match("foobar"));

    // Literal multi-segment path
    PATDEF(pat5, "a/b/c/d")
    EXPECT_TRUE(pat5.match("a/b/c/d"));
    EXPECT_FALSE(pat5.match("a/b/c"));
    EXPECT_FALSE(pat5.match("a/b/c/d/e"));
}

TEST_CASE(is_trivial_match_all) {
    auto p1 = kota::GlobPattern::create("**");
    EXPECT_TRUE(p1.has_value());
    EXPECT_TRUE(p1->is_trivial_match_all());

    auto p2 = kota::GlobPattern::create("*");
    EXPECT_TRUE(p2.has_value());
    EXPECT_TRUE(p2->is_trivial_match_all());

    auto p3 = kota::GlobPattern::create("**/*");
    EXPECT_TRUE(p3.has_value());
    EXPECT_FALSE(p3->is_trivial_match_all());

    auto p4 = kota::GlobPattern::create("foo/**");
    EXPECT_TRUE(p4.has_value());
    EXPECT_FALSE(p4->is_trivial_match_all());

    auto p5 = kota::GlobPattern::create("*.js");
    EXPECT_TRUE(p5.has_value());
    EXPECT_FALSE(p5->is_trivial_match_all());

    auto p6 = kota::GlobPattern::create("{a,b}");
    EXPECT_TRUE(p6.has_value());
    EXPECT_FALSE(p6->is_trivial_match_all());
}

TEST_CASE(single_star) {
    PATDEF(pat1, "*")
    EXPECT_TRUE(pat1.match("foo"));
    EXPECT_TRUE(pat1.match("bar.txt"));
    EXPECT_TRUE(pat1.match("a"));
    // In this implementation, standalone * is is_trivial_match_all and matches across segments
    EXPECT_TRUE(pat1.match("foo/bar"));
    EXPECT_TRUE(pat1.match("/foo"));

    // * in a segment (was rejected by old SubGlobPattern bug)
    PATDEF(pat2, "*/b")
    EXPECT_TRUE(pat2.match("a/b"));
    EXPECT_TRUE(pat2.match("foo/b"));
    EXPECT_FALSE(pat2.match("a/c"));
    EXPECT_FALSE(pat2.match("a/b/c"));

    // ? in a segment
    PATDEF(pat3, "?/b")
    EXPECT_TRUE(pat3.match("a/b"));
    EXPECT_TRUE(pat3.match("x/b"));
    EXPECT_FALSE(pat3.match("ab/b"));
    EXPECT_FALSE(pat3.match("/b"));
}

TEST_CASE(single_question) {
    PATDEF(pat1, "?")
    EXPECT_TRUE(pat1.match("a"));
    EXPECT_TRUE(pat1.match("z"));
    EXPECT_TRUE(pat1.match("0"));
    EXPECT_FALSE(pat1.match(""));
    EXPECT_FALSE(pat1.match("ab"));
    EXPECT_FALSE(pat1.match("/"));

    PATDEF(pat2, "??")
    EXPECT_TRUE(pat2.match("ab"));
    EXPECT_TRUE(pat2.match("12"));
    EXPECT_FALSE(pat2.match("a"));
    EXPECT_FALSE(pat2.match("abc"));

    PATDEF(pat3, "?.?")
    EXPECT_TRUE(pat3.match("a.b"));
    EXPECT_FALSE(pat3.match("ab.c"));
    EXPECT_FALSE(pat3.match("a.bc"));
}

TEST_CASE(star_matches_zero) {
    // * matches zero or more chars (doc says "zero or more")
    PATDEF(pat1, "*.cc")
    EXPECT_TRUE(pat1.match(".cc"));
    EXPECT_TRUE(pat1.match("foo.cc"));
    EXPECT_FALSE(pat1.match("foo.cpp"));

    PATDEF(pat2, "*foo")
    EXPECT_TRUE(pat2.match("foo"));
    EXPECT_TRUE(pat2.match("barfoo"));
}

TEST_CASE(trailing_slash) {
    // Pattern "foo/" — prefix is "foo/", no sub_globs
    // match("foo/") should be true (exact match)
    // match("foo") should be false (prefix doesn't match)
    PATDEF(pat1, "foo/")
    EXPECT_TRUE(pat1.match("foo/"));
    EXPECT_FALSE(pat1.match("foo"));
    EXPECT_FALSE(pat1.match("foo/bar"));
}

TEST_CASE(boundary_edge_cases) {
    // [^]] — in this implementation, ] immediately after ^ closes the bracket
    // because ^ is not ], so the ]-as-first-char rule doesn't apply.
    // The result is [^] (all non-/ chars) followed by literal ].
    PATDEF(pat1, "[^]]")
    EXPECT_TRUE(pat1.match("a]"));
    EXPECT_TRUE(pat1.match("0]"));
    EXPECT_FALSE(pat1.match("]"));
    EXPECT_FALSE(pat1.match("/]"));

    // {,a} — empty alternative in brace
    PATDEF(pat2, "{,a}")
    EXPECT_TRUE(pat2.match("a"));
    EXPECT_TRUE(pat2.match(""));

    // {a\,b,c} — escaped comma treated as literal inside brace
    // The brace parser sees \ and skips next char, so {a\,b,c} has terms: "a\,b" and "c"
    // SubGlobPattern::create then sees "a\,b" and treats \, as escaped comma
    PATDEF(pat3, R"({a\,b,c})")
    EXPECT_TRUE(pat3.match("a,b"));
    EXPECT_TRUE(pat3.match("c"));
    EXPECT_FALSE(pat3.match("a"));
    EXPECT_FALSE(pat3.match("b"));
}

TEST_CASE(match_empty_string) {
    PATDEF(pat1, "**")
    // ** matches any number of path segments including none
    // For empty string: s == s_end immediately, remaining pattern is "**"
    // find_first_not_of("*/") on "**" is npos, so returns true
    EXPECT_TRUE(pat1.match(""));

    PATDEF(pat2, "*")
    // * at s_end: find_first_not_of("*/", 0) on "*" is npos => true
    EXPECT_TRUE(pat2.match(""));

    PATDEF(pat3, "foo")
    EXPECT_FALSE(pat3.match(""));

    PATDEF(pat4, "*.js")
    EXPECT_FALSE(pat4.match(""));
}

TEST_CASE(inverted_bracket) {
    PATDEF(pat1, "[!a]")
    EXPECT_TRUE(pat1.match("b"));
    EXPECT_TRUE(pat1.match("z"));
    EXPECT_TRUE(pat1.match("0"));
    EXPECT_FALSE(pat1.match("a"));
    // Critical: inverted bracket must NOT match '/'
    EXPECT_FALSE(pat1.match("/"));

    PATDEF(pat2, "[!0-9]")
    EXPECT_TRUE(pat2.match("a"));
    EXPECT_FALSE(pat2.match("5"));
    EXPECT_FALSE(pat2.match("/"));

    PATDEF(pat3, "[^a-z]")
    EXPECT_TRUE(pat3.match("0"));
    EXPECT_TRUE(pat3.match("A"));
    EXPECT_FALSE(pat3.match("a"));
    EXPECT_FALSE(pat3.match("/"));
}

TEST_CASE(multiple_globstar) {
    PATDEF(pat1, "**/foo/**/bar")
    EXPECT_TRUE(pat1.match("foo/bar"));
    EXPECT_TRUE(pat1.match("a/foo/b/bar"));
    EXPECT_TRUE(pat1.match("a/b/foo/c/d/e/bar"));
    EXPECT_TRUE(pat1.match("/foo/bar"));
    EXPECT_TRUE(pat1.match("x/y/foo/z/bar"));
    EXPECT_FALSE(pat1.match("a/b/bar"));
    EXPECT_FALSE(pat1.match("foo/baz"));
    EXPECT_FALSE(pat1.match("foobar"));

    PATDEF(pat2, "**/a/**/b/**/c")
    EXPECT_TRUE(pat2.match("a/b/c"));
    EXPECT_TRUE(pat2.match("x/a/y/b/z/c"));
    EXPECT_FALSE(pat2.match("a/c"));
    EXPECT_FALSE(pat2.match("a/b"));
}

TEST_CASE(max_subpattern_limit) {
    // {a,b} x {c,d} = 4 subpatterns, limit 2 => fail
    auto p1 = kota::GlobPattern::create("{a,b}.{c,d}", 2);
    EXPECT_FALSE(p1.has_value());

    // Same with limit 4 => succeed
    auto p2 = kota::GlobPattern::create("{a,b}.{c,d}", 4);
    EXPECT_TRUE(p2.has_value());

    // Single brace with 3 terms, limit 2 => fail
    auto p3 = kota::GlobPattern::create("{a,b,c}", 2);
    EXPECT_FALSE(p3.has_value());

    // Limit 0 disables brace expansion, pattern kept as literal with braces
    auto p4 = kota::GlobPattern::create("{a,b}", 0);
    EXPECT_TRUE(p4.has_value());
    EXPECT_TRUE(p4->match("{a,b}"));
    EXPECT_FALSE(p4->match("a"));
    EXPECT_FALSE(p4->match("b"));

    // Limit 1 means only 1 subpattern allowed; single brace with 1 term is OK
    auto p5 = kota::GlobPattern::create("{a}", 1);
    EXPECT_TRUE(p5.has_value());
    EXPECT_TRUE(p5->match("a"));
}

TEST_CASE(bracket_at_start) {
    PATDEF(pat1, "[a-z]oo")
    EXPECT_TRUE(pat1.match("foo"));
    EXPECT_TRUE(pat1.match("boo"));
    EXPECT_FALSE(pat1.match("Foo"));
    EXPECT_FALSE(pat1.match("1oo"));
    EXPECT_FALSE(pat1.match("aoo/bar"));

    PATDEF(pat2, "[0-9]*")
    EXPECT_TRUE(pat2.match("1foo"));
    EXPECT_TRUE(pat2.match("9"));
    EXPECT_FALSE(pat2.match("a1"));
}

TEST_CASE(bracket_in_brace) {
    PATDEF(pat1, "{[a-z]oo,[0-9]ar}")
    EXPECT_TRUE(pat1.match("foo"));
    EXPECT_TRUE(pat1.match("boo"));
    EXPECT_TRUE(pat1.match("1ar"));
    EXPECT_TRUE(pat1.match("9ar"));
    EXPECT_FALSE(pat1.match("Foo"));
    EXPECT_FALSE(pat1.match("bar"));

    // Bracket with special chars inside brace
    PATDEF(pat2, R"({foo.[\*\?],bar})")
    EXPECT_TRUE(pat2.match("foo.*"));
    EXPECT_TRUE(pat2.match("foo.?"));
    EXPECT_TRUE(pat2.match("bar"));
    EXPECT_FALSE(pat2.match("foo.x"));
}

TEST_CASE(question_with_globstar) {
    PATDEF(pat1, "**/?.js")
    EXPECT_TRUE(pat1.match("a.js"));
    EXPECT_TRUE(pat1.match("foo/b.js"));
    EXPECT_TRUE(pat1.match("a/b/c.js"));
    // ** can absorb leading chars in the segment, so ab.js matches
    // because ** absorbs 'a' and ?.js matches 'b.js'
    EXPECT_TRUE(pat1.match("ab.js"));
    EXPECT_TRUE(pat1.match("foo/ab.js"));
    EXPECT_FALSE(pat1.match(".js"));

    PATDEF(pat2, "**/?")
    EXPECT_TRUE(pat2.match("a"));
    EXPECT_TRUE(pat2.match("foo/a"));
    EXPECT_TRUE(pat2.match("a/b/c/d"));
    // ** absorbs leading chars, so 'ab' matches (** absorbs 'a', ? matches 'b')
    EXPECT_TRUE(pat2.match("ab"));
    EXPECT_TRUE(pat2.match("foo/ab"));
}

TEST_CASE(globstar_slash) {
    PATDEF(pat1, "**/")
    EXPECT_TRUE(pat1.match("foo/bar"));
    EXPECT_TRUE(pat1.match("foo"));
    EXPECT_TRUE(pat1.match("/"));

    // ** followed by literal after /
    PATDEF(pat2, "**/x")
    EXPECT_TRUE(pat2.match("x"));
    EXPECT_TRUE(pat2.match("/x"));
    EXPECT_TRUE(pat2.match("a/b/c/x"));
    EXPECT_FALSE(pat2.match("ax"));
    EXPECT_FALSE(pat2.match("a/bx"));
}

TEST_CASE(backslash_in_input) {
    // ? should match a single backslash in the input
    PATDEF(pat1, "?")
    EXPECT_TRUE(pat1.match("\\"));

    // * should match strings containing backslashes
    PATDEF(pat2, "*")
    EXPECT_TRUE(pat2.match("a\\b"));
    EXPECT_TRUE(pat2.match("\\"));

    // Literal match with no special meaning of \ in input
    PATDEF(pat3, "**/*.txt")
    EXPECT_TRUE(pat3.match("path\\with\\backslash.txt"));
}

TEST_CASE(globstar_intermediate) {
    PATDEF(pat1, "**/*/foo")
    EXPECT_TRUE(pat1.match("a/foo"));
    EXPECT_TRUE(pat1.match("x/y/a/foo"));
    EXPECT_FALSE(pat1.match("foo"));

    PATDEF(pat2, "**/*.js/**/test")
    EXPECT_TRUE(pat2.match("foo.js/test"));
    EXPECT_TRUE(pat2.match("foo.js/a/b/test"));
    EXPECT_TRUE(pat2.match("a/foo.js/test"));
    EXPECT_FALSE(pat2.match("foo/test"));
}

};  // TEST_SUITE(glob_pattern)

}  // namespace

}  // namespace kota
