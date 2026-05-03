#include "kota/zest/zest.h"
#include "kota/support/glob_pattern.h"

namespace kota {

namespace {

#define PATDEF(NAME, PAT)                                                                          \
    auto Res##NAME = kota::GlobPattern::create(PAT, 100);                                          \
    EXPECT_TRUE(Res##NAME.has_value());                                                            \
    if(!Res##NAME.has_value())                                                                     \
        return;                                                                                    \
    auto NAME = std::move(*Res##NAME);

TEST_SUITE(GlobPattern) {

TEST_CASE(PatternSema) {
    auto Pat1 = kota::GlobPattern::create("**/****.{c,cc}", 100);
    EXPECT_FALSE(Pat1.has_value());

    auto Pat2 = kota::GlobPattern::create("/foo/bar/baz////aaa.{c,cc}", 100);
    EXPECT_FALSE(Pat2.has_value());

    auto Pat3 = kota::GlobPattern::create("/foo/bar/baz/**////*.{c,cc}", 100);
    EXPECT_FALSE(Pat3.has_value());
}

TEST_CASE(MaxSubGlob) {
    auto Pat1 = kota::GlobPattern::create("{AAA,BBB,AB*}");
    EXPECT_TRUE(Pat1.has_value());
    EXPECT_TRUE(Pat1->match("AAA"));
    EXPECT_TRUE(Pat1->match("BBB"));
    EXPECT_TRUE(Pat1->match("AB"));
    EXPECT_TRUE(Pat1->match("ABCD"));
    EXPECT_FALSE(Pat1->match("CCC"));
    EXPECT_TRUE(Pat1->match("ABCDE"));
}

TEST_CASE(Simple) {
    PATDEF(Pat1, "node_modules")
    EXPECT_TRUE(Pat1.match("node_modules"));
    EXPECT_FALSE(Pat1.match("node_module"));
    EXPECT_FALSE(Pat1.match("/node_modules"));
    EXPECT_FALSE(Pat1.match("test/node_modules"));

    PATDEF(Pat2, "test.txt")
    EXPECT_TRUE(Pat2.match("test.txt"));
    EXPECT_FALSE(Pat2.match("test?txt"));
    EXPECT_FALSE(Pat2.match("/text.txt"));
    EXPECT_FALSE(Pat2.match("test/test.txt"));

    PATDEF(Pat3, "test(.txt")
    EXPECT_TRUE(Pat3.match("test(.txt"));
    EXPECT_FALSE(Pat3.match("test?txt"));

    PATDEF(Pat4, "qunit")
    EXPECT_TRUE(Pat4.match("qunit"));
    EXPECT_FALSE(Pat4.match("qunit.css"));
    EXPECT_FALSE(Pat4.match("test/qunit"));

    PATDEF(Pat5, "/DNXConsoleApp/**/*.cs")
    EXPECT_TRUE(Pat5.match("/DNXConsoleApp/Program.cs"));
    EXPECT_TRUE(Pat5.match("/DNXConsoleApp/foo/Program.cs"));
}

TEST_CASE(DotHidden) {
    PATDEF(Pat1, ".*")
    EXPECT_TRUE(Pat1.match(".git"));
    EXPECT_TRUE(Pat1.match(".hidden.txt"));
    EXPECT_FALSE(Pat1.match("git"));
    EXPECT_FALSE(Pat1.match("hidden.txt"));
    EXPECT_FALSE(Pat1.match("path/.git"));
    EXPECT_FALSE(Pat1.match("path/.hidden.txt"));

    PATDEF(Pat2, "**/.*")
    EXPECT_TRUE(Pat2.match(".git"));
    EXPECT_TRUE(Pat2.match("/.git"));
    EXPECT_TRUE(Pat2.match(".hidden.txt"));
    EXPECT_FALSE(Pat2.match("git"));
    EXPECT_FALSE(Pat2.match("hidden.txt"));
    EXPECT_TRUE(Pat2.match("path/.git"));
    EXPECT_TRUE(Pat2.match("path/.hidden.txt"));
    EXPECT_TRUE(Pat2.match("/path/.git"));
    EXPECT_TRUE(Pat2.match("/path/.hidden.txt"));
    EXPECT_FALSE(Pat2.match("path/git"));
    EXPECT_FALSE(Pat2.match("pat.h/hidden.txt"));

    PATDEF(Pat3, "._*")
    EXPECT_TRUE(Pat3.match("._git"));
    EXPECT_TRUE(Pat3.match("._hidden.txt"));
    EXPECT_FALSE(Pat3.match("git"));
    EXPECT_FALSE(Pat3.match("hidden.txt"));
    EXPECT_FALSE(Pat3.match("path/._git"));
    EXPECT_FALSE(Pat3.match("path/._hidden.txt"));

    PATDEF(Pat4, "**/._*")
    EXPECT_TRUE(Pat4.match("._git"));
    EXPECT_TRUE(Pat4.match("._hidden.txt"));
    EXPECT_FALSE(Pat4.match("git"));
    EXPECT_FALSE(Pat4.match("hidden._txt"));
    EXPECT_TRUE(Pat4.match("path/._git"));
    EXPECT_TRUE(Pat4.match("path/._hidden.txt"));
    EXPECT_TRUE(Pat4.match("/path/._git"));
    EXPECT_TRUE(Pat4.match("/path/._hidden.txt"));
    EXPECT_FALSE(Pat4.match("path/git"));
    EXPECT_FALSE(Pat4.match("pat.h/hidden._txt"));
}

TEST_CASE(EscapeCharacter) {
    PATDEF(Pat1, R"(\*star)")
    EXPECT_TRUE(Pat1.match("*star"));

    PATDEF(Pat2, R"(\{\*\})")
    EXPECT_TRUE(Pat2.match("{*}"));
}

TEST_CASE(BracketExpr) {
    PATDEF(Pat1, R"([a-zA-Z\]])")
    EXPECT_TRUE(Pat1.match(R"(])"));
    EXPECT_FALSE(Pat1.match(R"([)"));
    EXPECT_TRUE(Pat1.match(R"(s)"));
    EXPECT_TRUE(Pat1.match(R"(S)"));
    EXPECT_FALSE(Pat1.match(R"(0)"));

    PATDEF(Pat2, R"([\\^a-zA-Z""\\])")
    EXPECT_TRUE(Pat2.match(R"(")"));
    EXPECT_TRUE(Pat2.match(R"(^)"));
    EXPECT_TRUE(Pat2.match(R"(\)"));
    EXPECT_TRUE(Pat2.match(R"(")"));
    EXPECT_TRUE(Pat2.match(R"(x)"));
    EXPECT_TRUE(Pat2.match(R"(X)"));
    EXPECT_FALSE(Pat2.match(R"(0)"));

    PATDEF(Pat3, R"([!0-9a-fA-F\-+\*])")
    EXPECT_FALSE(Pat3.match("1"));
    EXPECT_FALSE(Pat3.match("*"));
    EXPECT_TRUE(Pat3.match("s"));
    EXPECT_TRUE(Pat3.match("S"));
    EXPECT_TRUE(Pat3.match("H"));
    EXPECT_TRUE(Pat3.match("]"));

    PATDEF(Pat4, R"([^\^0-9a-fA-F\-+\*])")
    EXPECT_FALSE(Pat4.match("1"));
    EXPECT_FALSE(Pat4.match("*"));
    EXPECT_FALSE(Pat4.match("^"));
    EXPECT_TRUE(Pat4.match("s"));
    EXPECT_TRUE(Pat4.match("S"));
    EXPECT_TRUE(Pat4.match("H"));
    EXPECT_TRUE(Pat4.match("]"));

    PATDEF(Pat5, R"([\*-\^])")
    EXPECT_TRUE(Pat5.match("*"));
    EXPECT_FALSE(Pat5.match("a"));
    EXPECT_FALSE(Pat5.match("z"));
    EXPECT_TRUE(Pat5.match("A"));
    EXPECT_TRUE(Pat5.match("Z"));
    EXPECT_TRUE(Pat5.match("\\"));
    EXPECT_TRUE(Pat5.match("^"));
    EXPECT_TRUE(Pat5.match("-"));

    PATDEF(Pat6, "foo.[^0-9]")
    EXPECT_FALSE(Pat6.match("foo.5"));
    EXPECT_FALSE(Pat6.match("foo.8"));
    EXPECT_FALSE(Pat6.match("bar.5"));
    EXPECT_TRUE(Pat6.match("foo.f"));

    PATDEF(Pat7, "foo.[!0-9]")
    EXPECT_FALSE(Pat7.match("foo.5"));
    EXPECT_FALSE(Pat7.match("foo.8"));
    EXPECT_FALSE(Pat7.match("bar.5"));
    EXPECT_TRUE(Pat7.match("foo.f"));

    PATDEF(Pat8, "foo.[0!^*?]")
    EXPECT_FALSE(Pat8.match("foo.5"));
    EXPECT_FALSE(Pat8.match("foo.8"));
    EXPECT_TRUE(Pat8.match("foo.0"));
    EXPECT_TRUE(Pat8.match("foo.!"));
    EXPECT_TRUE(Pat8.match("foo.^"));
    EXPECT_TRUE(Pat8.match("foo.*"));
    EXPECT_TRUE(Pat8.match("foo.?"));

    PATDEF(Pat9, "foo[/]bar")
    EXPECT_FALSE(Pat9.match("foo/bar"));

    PATDEF(Pat10, "foo.[[]")
    EXPECT_TRUE(Pat10.match("foo.["));

    PATDEF(Pat11, "foo.[]]")
    EXPECT_TRUE(Pat11.match("foo.]"));

    PATDEF(Pat12, "foo.[][!]")
    EXPECT_TRUE(Pat12.match("foo.]"));
    EXPECT_TRUE(Pat12.match("foo.["));
    EXPECT_TRUE(Pat12.match("foo.!"));

    PATDEF(Pat13, "foo.[]-]")
    EXPECT_TRUE(Pat13.match("foo.]"));
    EXPECT_TRUE(Pat13.match("foo.-"));

    PATDEF(Pat14, "foo.[0-9]")
    EXPECT_TRUE(Pat14.match("foo.5"));
    EXPECT_TRUE(Pat14.match("foo.8"));
    EXPECT_FALSE(Pat14.match("bar.5"));
    EXPECT_FALSE(Pat14.match("foo.f"));
}

TEST_CASE(BraceExpr) {
    PATDEF(Pat1, "*foo[0-9a-z].{c,cpp,cppm,?pp}")
    EXPECT_FALSE(Pat1.match("foo1.cc"));
    EXPECT_TRUE(Pat1.match("foo2.cpp"));
    EXPECT_TRUE(Pat1.match("foo3.cppm"));
    EXPECT_TRUE(Pat1.match("foot.cppm"));
    EXPECT_TRUE(Pat1.match("foot.hpp"));
    EXPECT_TRUE(Pat1.match("foot.app"));
    EXPECT_FALSE(Pat1.match("fooD.cppm"));
    EXPECT_FALSE(Pat1.match("BarfooD.cppm"));
    EXPECT_FALSE(Pat1.match("foofooD.cppm"));

    PATDEF(Pat2, "proj/{build*,include,src}/*.{cc,cpp,h,hpp}")
    EXPECT_TRUE(Pat2.match("proj/include/foo.cc"));
    EXPECT_TRUE(Pat2.match("proj/include/bar.cpp"));
    EXPECT_FALSE(Pat2.match("proj/include/xxx/yyy/zzz/foo.cc"));
    EXPECT_TRUE(Pat2.match("proj/build-yyy/foo.h"));
    EXPECT_TRUE(Pat2.match("proj/build-xxx/foo.cpp"));
    EXPECT_TRUE(Pat2.match("proj/build/foo.cpp"));
    EXPECT_FALSE(Pat2.match("proj/build-xxx/xxx/yyy/zzz/foo.cpp"));

    PATDEF(Pat3, "*.{html,js}")
    EXPECT_TRUE(Pat3.match("foo.js"));
    EXPECT_TRUE(Pat3.match("foo.html"));
    EXPECT_FALSE(Pat3.match("folder/foo.js"));
    EXPECT_FALSE(Pat3.match("/node_modules/foo.js"));
    EXPECT_FALSE(Pat3.match("foo.jss"));
    EXPECT_FALSE(Pat3.match("some.js/test"));

    PATDEF(Pat4, "*.{html}")
    EXPECT_TRUE(Pat4.match("foo.html"));
    EXPECT_FALSE(Pat4.match("foo.js"));
    EXPECT_FALSE(Pat4.match("folder/foo.js"));
    EXPECT_FALSE(Pat4.match("/node_modules/foo.js"));
    EXPECT_FALSE(Pat4.match("foo.jss"));
    EXPECT_FALSE(Pat4.match("some.js/test"));

    PATDEF(Pat5, "{node_modules,testing}")
    EXPECT_TRUE(Pat5.match("node_modules"));
    EXPECT_TRUE(Pat5.match("testing"));
    EXPECT_FALSE(Pat5.match("node_module"));
    EXPECT_FALSE(Pat5.match("dtesting"));

    PATDEF(Pat6, "**/{foo,bar}")
    EXPECT_TRUE(Pat6.match("foo"));
    EXPECT_TRUE(Pat6.match("bar"));
    EXPECT_TRUE(Pat6.match("test/foo"));
    EXPECT_TRUE(Pat6.match("test/bar"));
    EXPECT_TRUE(Pat6.match("other/more/foo"));
    EXPECT_TRUE(Pat6.match("other/more/bar"));
    EXPECT_TRUE(Pat6.match("/foo"));
    EXPECT_TRUE(Pat6.match("/bar"));
    EXPECT_TRUE(Pat6.match("/test/foo"));
    EXPECT_TRUE(Pat6.match("/test/bar"));
    EXPECT_TRUE(Pat6.match("/other/more/foo"));
    EXPECT_TRUE(Pat6.match("/other/more/bar"));

    PATDEF(Pat7, "{foo,bar}/**")
    EXPECT_TRUE(Pat7.match("foo"));
    EXPECT_TRUE(Pat7.match("bar"));
    EXPECT_TRUE(Pat7.match("bar/"));
    EXPECT_TRUE(Pat7.match("foo/test"));
    EXPECT_TRUE(Pat7.match("bar/test"));
    EXPECT_TRUE(Pat7.match("bar/test/"));
    EXPECT_TRUE(Pat7.match("foo/other/more"));
    EXPECT_TRUE(Pat7.match("bar/other/more"));
    EXPECT_TRUE(Pat7.match("bar/other/more/"));

    PATDEF(Pat8, "{**/*.d.ts,**/*.js}")
    EXPECT_TRUE(Pat8.match("foo.js"));
    EXPECT_TRUE(Pat8.match("testing/foo.js"));
    EXPECT_TRUE(Pat8.match("/testing/foo.js"));
    EXPECT_TRUE(Pat8.match("foo.d.ts"));
    EXPECT_TRUE(Pat8.match("testing/foo.d.ts"));
    EXPECT_TRUE(Pat8.match("/testing/foo.d.ts"));
    EXPECT_FALSE(Pat8.match("foo.d"));
    EXPECT_FALSE(Pat8.match("testing/foo.d"));
    EXPECT_FALSE(Pat8.match("/testing/foo.d"));

    PATDEF(Pat9, "{**/*.d.ts,**/*.js,path/simple.jgs}")
    EXPECT_TRUE(Pat9.match("foo.js"));
    EXPECT_TRUE(Pat9.match("testing/foo.js"));
    EXPECT_TRUE(Pat9.match("/testing/foo.js"));
    EXPECT_TRUE(Pat9.match("path/simple.jgs"));
    EXPECT_FALSE(Pat9.match("/path/simple.jgs"));

    PATDEF(Pat10, "{**/*.d.ts,**/*.js,foo.[0-9]}")
    EXPECT_TRUE(Pat10.match("foo.5"));
    EXPECT_TRUE(Pat10.match("foo.8"));
    EXPECT_FALSE(Pat10.match("bar.5"));
    EXPECT_FALSE(Pat10.match("foo.f"));
    EXPECT_TRUE(Pat10.match("foo.js"));

    PATDEF(Pat11, "prefix/{**/*.d.ts,**/*.js,foo.[0-9]}")
    EXPECT_TRUE(Pat11.match("prefix/foo.5"));
    EXPECT_TRUE(Pat11.match("prefix/foo.8"));
    EXPECT_FALSE(Pat11.match("prefix/bar.5"));
    EXPECT_FALSE(Pat11.match("prefix/foo.f"));
    EXPECT_TRUE(Pat11.match("prefix/foo.js"));
}

TEST_CASE(GlobstarPrefix) {
    // **/* — match any path
    PATDEF(Pat1, "**/*")
    EXPECT_TRUE(Pat1.match("foo"));
    EXPECT_TRUE(Pat1.match("foo/bar"));
    EXPECT_TRUE(Pat1.match("foo/bar/baz"));

    // **/[0-9]* — last segment starts with digit
    PATDEF(Pat2, "**/[0-9]*")
    EXPECT_TRUE(Pat2.match("114514foo"));
    EXPECT_FALSE(Pat2.match("foo/bar/baz/xxx/yyy/zzz"));
    EXPECT_FALSE(Pat2.match("foo/bar/baz/xxx/yyy/zzz114514"));
    EXPECT_TRUE(Pat2.match("foo/bar/baz/xxx/yyy/114514"));
    EXPECT_TRUE(Pat2.match("foo/bar/baz/xxx/yyy/114514zzz"));

    // **/*[0-9] — last segment ends with digit
    PATDEF(Pat3, "**/*[0-9]")
    EXPECT_TRUE(Pat3.match("foo5"));
    EXPECT_FALSE(Pat3.match("foo/bar/baz/xxx/yyy/zzz"));
    EXPECT_TRUE(Pat3.match("foo/bar/baz/xxx/yyy/zzz114514"));

    // **/include/test/*.{cc,...} — globstar prefix with multi-segment literal
    PATDEF(Pat4, "**/include/test/*.{cc,hh,c,h,cpp,hpp}")
    EXPECT_TRUE(Pat4.match("include/test/aaa.cc"));
    EXPECT_TRUE(Pat4.match("/include/test/aaa.cc"));
    EXPECT_TRUE(Pat4.match("xxx/yyy/include/test/aaa.cc"));
    EXPECT_TRUE(Pat4.match("include/foo/bar/baz/include/test/bbb.hh"));
    EXPECT_TRUE(Pat4.match("include/include/include/include/include/test/bbb.hpp"));

    // **include/test/*.{cc,...} — globstar attached to literal (no slash after **)
    PATDEF(Pat5, "**include/test/*.{cc,hh,c,h,cpp,hpp}")
    EXPECT_TRUE(Pat5.match("include/test/fff.hpp"));
    EXPECT_TRUE(Pat5.match("xxx-yyy-include/test/fff.hpp"));
    EXPECT_TRUE(Pat5.match("xxx-yyy-include/test/.hpp"));
    EXPECT_TRUE(Pat5.match("/include/test/aaa.cc"));
    EXPECT_TRUE(Pat5.match("include/foo/bar/baz/include/test/bbb.hh"));

    // **/*foo.{c,cpp} — globstar prefix with wildcard suffix
    PATDEF(Pat6, "**/*foo.{c,cpp}")
    EXPECT_TRUE(Pat6.match("bar/foo.cpp"));
    EXPECT_TRUE(Pat6.match("bar/barfoo.cpp"));
    EXPECT_TRUE(Pat6.match("/foofoo.cpp"));
    EXPECT_TRUE(Pat6.match("foo/foo/foo/foo/foofoo.cpp"));
    EXPECT_TRUE(Pat6.match("foofoo.cpp"));
    EXPECT_TRUE(Pat6.match("barfoo.cpp"));
    EXPECT_TRUE(Pat6.match("foo.cpp"));

    // ** — matches everything
    PATDEF(Pat7, "**")
    EXPECT_TRUE(Pat7.match("foo"));
    EXPECT_TRUE(Pat7.match("foo/bar/baz"));
    EXPECT_TRUE(Pat7.match("/"));
    EXPECT_TRUE(Pat7.match("foo.js"));
    EXPECT_TRUE(Pat7.match("folder/foo.js"));
    EXPECT_TRUE(Pat7.match("folder/foo/"));
    EXPECT_TRUE(Pat7.match("/node_modules/foo.js"));
    EXPECT_TRUE(Pat7.match("foo.jss"));
    EXPECT_TRUE(Pat7.match("some.js/test"));

    // **/x — match literal at any depth
    PATDEF(Pat8, "**/x")
    EXPECT_TRUE(Pat8.match("x"));
    EXPECT_TRUE(Pat8.match("/x"));
    EXPECT_TRUE(Pat8.match("/x/x/x/x/x"));

    // **/*.{cc,cpp} — extension match at any depth
    PATDEF(Pat9, "**/*.{cc,cpp}")
    EXPECT_TRUE(Pat9.match("foo/bar/baz.cc"));
    EXPECT_TRUE(Pat9.match("foo/foo/foo.cpp"));
    EXPECT_TRUE(Pat9.match("foo/bar/.cc"));

    // **/*?.{cc,cpp} — wildcard then question before extension
    PATDEF(Pat10, "**/*?.{cc,cpp}")
    EXPECT_TRUE(Pat10.match("foo/bar/baz/xxx/yyy/zzz/aaa.cc"));
    EXPECT_TRUE(Pat10.match("foo/bar/baz/xxx/yyy/zzz/a.cc"));
    EXPECT_FALSE(Pat10.match("foo/bar/baz/xxx/yyy/zzz/.cc"));

    // **/?*.{cc,cpp} — question then wildcard before extension
    // After ?* special case removal, ? matches one char and * matches rest independently.
    // With ** backtracking, the * can hop across / boundaries, so .cc after a / is matched
    // when ** absorbs enough of the prefix.
    PATDEF(Pat11, "**/?*.{cc,cpp}")
    EXPECT_TRUE(Pat11.match("foo/bar/baz/xxx/yyy/zzz/aaa.cc"));
    EXPECT_TRUE(Pat11.match("foo/bar/baz/xxx/yyy/zzz/a.cc"));
    EXPECT_TRUE(Pat11.match("foo/bar/baz/xxx/yyy/zzz/.cc"));

    // **/*.js — JS file at any depth
    PATDEF(Pat12, "**/*.js")
    EXPECT_TRUE(Pat12.match("foo.js"));
    EXPECT_TRUE(Pat12.match("/foo.js"));
    EXPECT_TRUE(Pat12.match("folder/foo.js"));
    EXPECT_TRUE(Pat12.match("/node_modules/foo.js"));
    EXPECT_FALSE(Pat12.match("foo.jss"));
    EXPECT_FALSE(Pat12.match("some.js/test"));
    EXPECT_FALSE(Pat12.match("/some.js/test"));

    // **/project.json — exact filename at any depth
    PATDEF(Pat13, "**/project.json")
    EXPECT_TRUE(Pat13.match("project.json"));
    EXPECT_TRUE(Pat13.match("/project.json"));
    EXPECT_TRUE(Pat13.match("some/folder/project.json"));
    EXPECT_TRUE(Pat13.match("/some/folder/project.json"));
    EXPECT_FALSE(Pat13.match("some/folder/file_project.json"));
    EXPECT_FALSE(Pat13.match("some/folder/fileproject.json"));
    EXPECT_FALSE(Pat13.match("some/rrproject.json"));
}

TEST_CASE(GlobstarSuffix) {
    // x/** — everything under x/
    PATDEF(Pat1, "x/**")
    EXPECT_TRUE(Pat1.match("x/"));
    EXPECT_TRUE(Pat1.match("x/foo/bar/baz"));
    EXPECT_TRUE(Pat1.match("x"));

    // test/** — everything under test/
    PATDEF(Pat2, "test/**")
    EXPECT_TRUE(Pat2.match("test"));
    EXPECT_TRUE(Pat2.match("test/foo"));
    EXPECT_TRUE(Pat2.match("test/foo/"));
    EXPECT_TRUE(Pat2.match("test/foo.js"));
    EXPECT_TRUE(Pat2.match("test/other/foo.js"));
    EXPECT_FALSE(Pat2.match("est/other/foo.js"));
}

TEST_CASE(GlobstarMiddle) {
    // test/**/*.js — JS files under test/ at any depth
    PATDEF(Pat1, "test/**/*.js")
    EXPECT_TRUE(Pat1.match("test/foo.js"));
    EXPECT_TRUE(Pat1.match("test/other/foo.js"));
    EXPECT_TRUE(Pat1.match("test/other/more/foo.js"));
    EXPECT_FALSE(Pat1.match("test/foo.ts"));
    EXPECT_FALSE(Pat1.match("test/other/foo.ts"));
    EXPECT_FALSE(Pat1.match("test/other/more/foo.ts"));

    // some/**/*.js — JS files under some/ at any depth
    PATDEF(Pat2, "some/**/*.js")
    EXPECT_TRUE(Pat2.match("some/foo.js"));
    EXPECT_TRUE(Pat2.match("some/folder/foo.js"));
    EXPECT_FALSE(Pat2.match("something/foo.js"));
    EXPECT_FALSE(Pat2.match("something/folder/foo.js"));

    // some/**/* — any file under some/ at any depth
    PATDEF(Pat3, "some/**/*")
    EXPECT_TRUE(Pat3.match("some/foo.js"));
    EXPECT_TRUE(Pat3.match("some/folder/foo.js"));
    EXPECT_FALSE(Pat3.match("something/foo.js"));
    EXPECT_FALSE(Pat3.match("something/folder/foo.js"));
}

TEST_CASE(GlobstarComplex) {
    // **/**/*.js — double globstar
    PATDEF(Pat1, "**/**/*.js")
    EXPECT_TRUE(Pat1.match("foo.js"));
    EXPECT_TRUE(Pat1.match("/foo.js"));
    EXPECT_TRUE(Pat1.match("folder/foo.js"));
    EXPECT_TRUE(Pat1.match("/node_modules/foo.js"));
    EXPECT_FALSE(Pat1.match("foo.jss"));
    EXPECT_FALSE(Pat1.match("some.js/test"));

    // **/node_modules/**/*.js — scoped to node_modules
    PATDEF(Pat2, "**/node_modules/**/*.js")
    EXPECT_FALSE(Pat2.match("foo.js"));
    EXPECT_FALSE(Pat2.match("folder/foo.js"));
    EXPECT_TRUE(Pat2.match("node_modules/foo.js"));
    EXPECT_TRUE(Pat2.match("/node_modules/foo.js"));
    EXPECT_TRUE(Pat2.match("node_modules/some/folder/foo.js"));
    EXPECT_TRUE(Pat2.match("/node_modules/some/folder/foo.js"));
    EXPECT_FALSE(Pat2.match("node_modules/some/folder/foo.ts"));
    EXPECT_FALSE(Pat2.match("foo.jss"));
    EXPECT_FALSE(Pat2.match("some.js/test"));

    // Brace with multiple globstar patterns
    PATDEF(Pat3, "{**/node_modules/**,**/.git/**,**/bower_components/**}")
    EXPECT_TRUE(Pat3.match("node_modules"));
    EXPECT_TRUE(Pat3.match("/node_modules"));
    EXPECT_TRUE(Pat3.match("/node_modules/more"));
    EXPECT_TRUE(Pat3.match("some/test/node_modules"));
    EXPECT_TRUE(Pat3.match("/some/test/node_modules"));
    EXPECT_TRUE(Pat3.match("bower_components"));
    EXPECT_TRUE(Pat3.match("bower_components/more"));
    EXPECT_TRUE(Pat3.match("/bower_components"));
    EXPECT_TRUE(Pat3.match("some/test/bower_components"));
    EXPECT_TRUE(Pat3.match("/some/test/bower_components"));
    EXPECT_TRUE(Pat3.match(".git"));
    EXPECT_TRUE(Pat3.match("/.git"));
    EXPECT_TRUE(Pat3.match("some/test/.git"));
    EXPECT_TRUE(Pat3.match("/some/test/.git"));
    EXPECT_FALSE(Pat3.match("tempting"));
    EXPECT_FALSE(Pat3.match("/tempting"));
    EXPECT_FALSE(Pat3.match("some/test/tempting"));
    EXPECT_FALSE(Pat3.match("/some/test/tempting"));

    // Brace with multiple globstar-prefixed patterns
    PATDEF(Pat4, "{**/package.json,**/project.json}")
    EXPECT_TRUE(Pat4.match("package.json"));
    EXPECT_TRUE(Pat4.match("/package.json"));
    EXPECT_FALSE(Pat4.match("xpackage.json"));
    EXPECT_FALSE(Pat4.match("/xpackage.json"));
}

TEST_CASE(ErrorPaths) {
    // Unmatched '['
    auto E1 = kota::GlobPattern::create("foo.[a-z");
    EXPECT_FALSE(E1.has_value());

    // '[' as last character
    auto E2 = kota::GlobPattern::create("{a,[}");
    EXPECT_FALSE(E2.has_value());

    // Stray '\' at end of pattern (in SubGlobPattern)
    auto E3 = kota::GlobPattern::create("foo\\");
    EXPECT_FALSE(E3.has_value());

    // Stray '\' at end inside brace
    auto E4 = kota::GlobPattern::create("{foo\\}");
    EXPECT_FALSE(E4.has_value());

    // Stray '\' inside bracket inside brace
    auto E5 = kota::GlobPattern::create("{[abc\\]}");
    EXPECT_FALSE(E5.has_value());

    // Empty brace expression {}
    auto E6 = kota::GlobPattern::create("foo.{}");
    EXPECT_FALSE(E6.has_value());

    // Nested braces
    auto E7 = kota::GlobPattern::create("{a,{b,c}}");
    EXPECT_FALSE(E7.has_value());

    // Incomplete brace expansion (unmatched '{')
    auto E8 = kota::GlobPattern::create("{foo,bar");
    EXPECT_FALSE(E8.has_value());

    // *** (triple star)
    auto E9 = kota::GlobPattern::create("***.js");
    EXPECT_FALSE(E9.has_value());

    // ** is valid (boundary)
    auto E10 = kota::GlobPattern::create("**.js");
    EXPECT_TRUE(E10.has_value());

    // Multiple consecutive slashes in literal pattern
    auto E11 = kota::GlobPattern::create("foo//bar");
    EXPECT_FALSE(E11.has_value());

    // Multiple consecutive slashes at start
    auto E12 = kota::GlobPattern::create("//foo");
    EXPECT_FALSE(E12.has_value());

    // Multiple consecutive slashes in glob pattern (detected by SubGlobPattern)
    auto E13 = kota::GlobPattern::create("**/foo//*.cc");
    EXPECT_FALSE(E13.has_value());

    // Unmatched '[' in SubGlobPattern
    auto E14 = kota::GlobPattern::create("*[");
    EXPECT_FALSE(E14.has_value());

    // '\' at end inside bracket inside brace
    auto E15 = kota::GlobPattern::create("{[\\]}");
    EXPECT_FALSE(E15.has_value());

    // Range start > end
    auto E16 = kota::GlobPattern::create("[z-a]");
    EXPECT_FALSE(E16.has_value());

    // Range end is stray backslash
    auto E17 = kota::GlobPattern::create("[a-\\]");
    EXPECT_FALSE(E17.has_value());
}

TEST_CASE(EmptyAndTrivial) {
    // Empty pattern matches only empty string
    PATDEF(Pat1, "")
    EXPECT_TRUE(Pat1.match(""));
    EXPECT_FALSE(Pat1.match("foo"));
    EXPECT_FALSE(Pat1.match("/"));

    // Single character pattern
    PATDEF(Pat2, "a")
    EXPECT_TRUE(Pat2.match("a"));
    EXPECT_FALSE(Pat2.match("b"));
    EXPECT_FALSE(Pat2.match("ab"));
    EXPECT_FALSE(Pat2.match(""));

    // Slash-only pattern
    PATDEF(Pat3, "/")
    EXPECT_TRUE(Pat3.match("/"));
    EXPECT_FALSE(Pat3.match(""));
    EXPECT_FALSE(Pat3.match("//"));

    // Literal path with slashes (was rejected before bug fix)
    PATDEF(Pat4, "foo/bar")
    EXPECT_TRUE(Pat4.match("foo/bar"));
    EXPECT_FALSE(Pat4.match("foo/baz"));
    EXPECT_FALSE(Pat4.match("foo/bar/baz"));
    EXPECT_FALSE(Pat4.match("foobar"));

    // Literal multi-segment path
    PATDEF(Pat5, "a/b/c/d")
    EXPECT_TRUE(Pat5.match("a/b/c/d"));
    EXPECT_FALSE(Pat5.match("a/b/c"));
    EXPECT_FALSE(Pat5.match("a/b/c/d/e"));
}

TEST_CASE(IsTrivialMatchAll) {
    auto P1 = kota::GlobPattern::create("**");
    EXPECT_TRUE(P1.has_value());
    EXPECT_TRUE(P1->isTrivialMatchAll());

    auto P2 = kota::GlobPattern::create("*");
    EXPECT_TRUE(P2.has_value());
    EXPECT_TRUE(P2->isTrivialMatchAll());

    auto P3 = kota::GlobPattern::create("**/*");
    EXPECT_TRUE(P3.has_value());
    EXPECT_FALSE(P3->isTrivialMatchAll());

    auto P4 = kota::GlobPattern::create("foo/**");
    EXPECT_TRUE(P4.has_value());
    EXPECT_FALSE(P4->isTrivialMatchAll());

    auto P5 = kota::GlobPattern::create("*.js");
    EXPECT_TRUE(P5.has_value());
    EXPECT_FALSE(P5->isTrivialMatchAll());

    auto P6 = kota::GlobPattern::create("{a,b}");
    EXPECT_TRUE(P6.has_value());
    EXPECT_FALSE(P6->isTrivialMatchAll());
}

TEST_CASE(SingleStar) {
    PATDEF(Pat1, "*")
    EXPECT_TRUE(Pat1.match("foo"));
    EXPECT_TRUE(Pat1.match("bar.txt"));
    EXPECT_TRUE(Pat1.match("a"));
    // In this implementation, standalone * is isTrivialMatchAll and matches across segments
    EXPECT_TRUE(Pat1.match("foo/bar"));
    EXPECT_TRUE(Pat1.match("/foo"));

    // * in a segment (was rejected by old SubGlobPattern bug)
    PATDEF(Pat2, "*/b")
    EXPECT_TRUE(Pat2.match("a/b"));
    EXPECT_TRUE(Pat2.match("foo/b"));
    EXPECT_FALSE(Pat2.match("a/c"));
    EXPECT_FALSE(Pat2.match("a/b/c"));

    // ? in a segment
    PATDEF(Pat3, "?/b")
    EXPECT_TRUE(Pat3.match("a/b"));
    EXPECT_TRUE(Pat3.match("x/b"));
    EXPECT_FALSE(Pat3.match("ab/b"));
    EXPECT_FALSE(Pat3.match("/b"));
}

TEST_CASE(SingleQuestion) {
    PATDEF(Pat1, "?")
    EXPECT_TRUE(Pat1.match("a"));
    EXPECT_TRUE(Pat1.match("z"));
    EXPECT_TRUE(Pat1.match("0"));
    EXPECT_FALSE(Pat1.match(""));
    EXPECT_FALSE(Pat1.match("ab"));
    EXPECT_FALSE(Pat1.match("/"));

    PATDEF(Pat2, "??")
    EXPECT_TRUE(Pat2.match("ab"));
    EXPECT_TRUE(Pat2.match("12"));
    EXPECT_FALSE(Pat2.match("a"));
    EXPECT_FALSE(Pat2.match("abc"));

    PATDEF(Pat3, "?.?")
    EXPECT_TRUE(Pat3.match("a.b"));
    EXPECT_FALSE(Pat3.match("ab.c"));
    EXPECT_FALSE(Pat3.match("a.bc"));
}

TEST_CASE(StarMatchesZero) {
    // * matches zero or more chars (doc says "zero or more")
    PATDEF(Pat1, "*.cc")
    EXPECT_TRUE(Pat1.match(".cc"));
    EXPECT_TRUE(Pat1.match("foo.cc"));
    EXPECT_FALSE(Pat1.match("foo.cpp"));

    PATDEF(Pat2, "*foo")
    EXPECT_TRUE(Pat2.match("foo"));
    EXPECT_TRUE(Pat2.match("barfoo"));
}

TEST_CASE(TrailingSlashPrefix) {
    // Pattern "foo/" — prefix is "foo/", no sub_globs
    // match("foo/") should be true (exact match)
    // match("foo") should be false (prefix doesn't match)
    PATDEF(Pat1, "foo/")
    EXPECT_TRUE(Pat1.match("foo/"));
    EXPECT_FALSE(Pat1.match("foo"));
    EXPECT_FALSE(Pat1.match("foo/bar"));
}

TEST_CASE(BoundaryEdgeCases) {
    // [^]] — in this implementation, ] immediately after ^ closes the bracket
    // because ^ is not ], so the ]-as-first-char rule doesn't apply.
    // The result is [^] (all non-/ chars) followed by literal ].
    PATDEF(Pat1, "[^]]")
    EXPECT_TRUE(Pat1.match("a]"));
    EXPECT_TRUE(Pat1.match("0]"));
    EXPECT_FALSE(Pat1.match("]"));
    EXPECT_FALSE(Pat1.match("/]"));

    // {,a} — empty alternative in brace
    PATDEF(Pat2, "{,a}")
    EXPECT_TRUE(Pat2.match("a"));
    EXPECT_TRUE(Pat2.match(""));

    // {a\,b,c} — escaped comma treated as literal inside brace
    // The brace parser sees \ and skips next char, so {a\,b,c} has terms: "a\,b" and "c"
    // SubGlobPattern::create then sees "a\,b" and treats \, as escaped comma
    PATDEF(Pat3, R"({a\,b,c})")
    EXPECT_TRUE(Pat3.match("a,b"));
    EXPECT_TRUE(Pat3.match("c"));
    EXPECT_FALSE(Pat3.match("a"));
    EXPECT_FALSE(Pat3.match("b"));
}

TEST_CASE(MatchEmptyString) {
    PATDEF(Pat1, "**")
    // ** matches any number of path segments including none
    // For empty string: s == s_end immediately, remaining pattern is "**"
    // find_first_not_of("*/") on "**" is npos, so returns true
    EXPECT_TRUE(Pat1.match(""));

    PATDEF(Pat2, "*")
    // * at s_end: find_first_not_of("*/", 0) on "*" is npos => true
    EXPECT_TRUE(Pat2.match(""));

    PATDEF(Pat3, "foo")
    EXPECT_FALSE(Pat3.match(""));

    PATDEF(Pat4, "*.js")
    EXPECT_FALSE(Pat4.match(""));
}

TEST_CASE(InvertedBracket) {
    PATDEF(Pat1, "[!a]")
    EXPECT_TRUE(Pat1.match("b"));
    EXPECT_TRUE(Pat1.match("z"));
    EXPECT_TRUE(Pat1.match("0"));
    EXPECT_FALSE(Pat1.match("a"));
    // Critical: inverted bracket must NOT match '/'
    EXPECT_FALSE(Pat1.match("/"));

    PATDEF(Pat2, "[!0-9]")
    EXPECT_TRUE(Pat2.match("a"));
    EXPECT_FALSE(Pat2.match("5"));
    EXPECT_FALSE(Pat2.match("/"));

    PATDEF(Pat3, "[^a-z]")
    EXPECT_TRUE(Pat3.match("0"));
    EXPECT_TRUE(Pat3.match("A"));
    EXPECT_FALSE(Pat3.match("a"));
    EXPECT_FALSE(Pat3.match("/"));
}

TEST_CASE(MultipleGlobstar) {
    PATDEF(Pat1, "**/foo/**/bar")
    EXPECT_TRUE(Pat1.match("foo/bar"));
    EXPECT_TRUE(Pat1.match("a/foo/b/bar"));
    EXPECT_TRUE(Pat1.match("a/b/foo/c/d/e/bar"));
    EXPECT_TRUE(Pat1.match("/foo/bar"));
    EXPECT_TRUE(Pat1.match("x/y/foo/z/bar"));
    EXPECT_FALSE(Pat1.match("a/b/bar"));
    EXPECT_FALSE(Pat1.match("foo/baz"));
    EXPECT_FALSE(Pat1.match("foobar"));

    PATDEF(Pat2, "**/a/**/b/**/c")
    EXPECT_TRUE(Pat2.match("a/b/c"));
    EXPECT_TRUE(Pat2.match("x/a/y/b/z/c"));
    EXPECT_FALSE(Pat2.match("a/c"));
    EXPECT_FALSE(Pat2.match("a/b"));
}

TEST_CASE(MaxSubpatternLimit) {
    // {a,b} x {c,d} = 4 subpatterns, limit 2 => fail
    auto P1 = kota::GlobPattern::create("{a,b}.{c,d}", 2);
    EXPECT_FALSE(P1.has_value());

    // Same with limit 4 => succeed
    auto P2 = kota::GlobPattern::create("{a,b}.{c,d}", 4);
    EXPECT_TRUE(P2.has_value());

    // Single brace with 3 terms, limit 2 => fail
    auto P3 = kota::GlobPattern::create("{a,b,c}", 2);
    EXPECT_FALSE(P3.has_value());

    // Limit 0 disables brace expansion, pattern kept as literal with braces
    auto P4 = kota::GlobPattern::create("{a,b}", 0);
    EXPECT_TRUE(P4.has_value());
    EXPECT_TRUE(P4->match("{a,b}"));
    EXPECT_FALSE(P4->match("a"));
    EXPECT_FALSE(P4->match("b"));

    // Limit 1 means only 1 subpattern allowed; single brace with 1 term is OK
    auto P5 = kota::GlobPattern::create("{a}", 1);
    EXPECT_TRUE(P5.has_value());
    EXPECT_TRUE(P5->match("a"));
}

TEST_CASE(BracketAtStart) {
    PATDEF(Pat1, "[a-z]oo")
    EXPECT_TRUE(Pat1.match("foo"));
    EXPECT_TRUE(Pat1.match("boo"));
    EXPECT_FALSE(Pat1.match("Foo"));
    EXPECT_FALSE(Pat1.match("1oo"));
    EXPECT_FALSE(Pat1.match("aoo/bar"));

    PATDEF(Pat2, "[0-9]*")
    EXPECT_TRUE(Pat2.match("1foo"));
    EXPECT_TRUE(Pat2.match("9"));
    EXPECT_FALSE(Pat2.match("a1"));
}

TEST_CASE(BracketInBrace) {
    PATDEF(Pat1, "{[a-z]oo,[0-9]ar}")
    EXPECT_TRUE(Pat1.match("foo"));
    EXPECT_TRUE(Pat1.match("boo"));
    EXPECT_TRUE(Pat1.match("1ar"));
    EXPECT_TRUE(Pat1.match("9ar"));
    EXPECT_FALSE(Pat1.match("Foo"));
    EXPECT_FALSE(Pat1.match("bar"));

    // Bracket with special chars inside brace
    PATDEF(Pat2, R"({foo.[\*\?],bar})")
    EXPECT_TRUE(Pat2.match("foo.*"));
    EXPECT_TRUE(Pat2.match("foo.?"));
    EXPECT_TRUE(Pat2.match("bar"));
    EXPECT_FALSE(Pat2.match("foo.x"));
}

TEST_CASE(QuestionWithGlobstar) {
    PATDEF(Pat1, "**/?.js")
    EXPECT_TRUE(Pat1.match("a.js"));
    EXPECT_TRUE(Pat1.match("foo/b.js"));
    EXPECT_TRUE(Pat1.match("a/b/c.js"));
    // ** can absorb leading chars in the segment, so ab.js matches
    // because ** absorbs 'a' and ?.js matches 'b.js'
    EXPECT_TRUE(Pat1.match("ab.js"));
    EXPECT_TRUE(Pat1.match("foo/ab.js"));
    EXPECT_FALSE(Pat1.match(".js"));

    PATDEF(Pat2, "**/?")
    EXPECT_TRUE(Pat2.match("a"));
    EXPECT_TRUE(Pat2.match("foo/a"));
    EXPECT_TRUE(Pat2.match("a/b/c/d"));
    // ** absorbs leading chars, so 'ab' matches (** absorbs 'a', ? matches 'b')
    EXPECT_TRUE(Pat2.match("ab"));
    EXPECT_TRUE(Pat2.match("foo/ab"));
}

TEST_CASE(GlobstarSlashPatterns) {
    PATDEF(Pat1, "**/")
    EXPECT_TRUE(Pat1.match("foo/bar"));
    EXPECT_TRUE(Pat1.match("foo"));
    EXPECT_TRUE(Pat1.match("/"));

    // ** followed by literal after /
    PATDEF(Pat2, "**/x")
    EXPECT_TRUE(Pat2.match("x"));
    EXPECT_TRUE(Pat2.match("/x"));
    EXPECT_TRUE(Pat2.match("a/b/c/x"));
    EXPECT_FALSE(Pat2.match("ax"));
    EXPECT_FALSE(Pat2.match("a/bx"));
}

TEST_CASE(BackslashInInput) {
    // ? should match a single backslash in the input
    PATDEF(Pat1, "?")
    EXPECT_TRUE(Pat1.match("\\"));

    // * should match strings containing backslashes
    PATDEF(Pat2, "*")
    EXPECT_TRUE(Pat2.match("a\\b"));
    EXPECT_TRUE(Pat2.match("\\"));

    // Literal match with no special meaning of \ in input
    PATDEF(Pat3, "**/*.txt")
    EXPECT_TRUE(Pat3.match("path\\with\\backslash.txt"));
}

TEST_CASE(GlobstarWithIntermediateSegments) {
    PATDEF(Pat1, "**/*/foo")
    EXPECT_TRUE(Pat1.match("a/foo"));
    EXPECT_TRUE(Pat1.match("x/y/a/foo"));
    EXPECT_FALSE(Pat1.match("foo"));

    PATDEF(Pat2, "**/*.js/**/test")
    EXPECT_TRUE(Pat2.match("foo.js/test"));
    EXPECT_TRUE(Pat2.match("foo.js/a/b/test"));
    EXPECT_TRUE(Pat2.match("a/foo.js/test"));
    EXPECT_FALSE(Pat2.match("foo/test"));
}

};  // TEST_SUITE(GlobPattern)

}  // namespace

}  // namespace kota
