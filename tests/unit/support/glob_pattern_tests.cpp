#include <algorithm>
#include <array>
#include <memory>
#include <random>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/support/glob_pattern.h"

namespace kota {

namespace {

#define PATDEF(name, pat_str)                                                                      \
    auto _res_##name = kota::GlobPattern::create(pat_str, 100);                                    \
    EXPECT(_res_##name.has_value());                                                               \
    if(!_res_##name.has_value())                                                                   \
        return;                                                                                    \
    auto name = std::move(*_res_##name);

ZEST_SUITE(glob_pattern){

    ZEST_CASE(pattern_sema){auto pat1 = kota::GlobPattern::create("**/****.{c,cc}", 100);
EXPECT(!pat1.has_value());

auto pat2 = kota::GlobPattern::create("/foo/bar/baz////aaa.{c,cc}", 100);
EXPECT(!pat2.has_value());

auto pat3 = kota::GlobPattern::create("/foo/bar/baz/**////*.{c,cc}", 100);
EXPECT(!pat3.has_value());

EXPECT(!kota::GlobPattern::create("foo//*.cc").has_value());
EXPECT(kota::GlobPattern::create("/usr/bin/*.txt").has_value());

}  // namespace

ZEST_CASE(max_sub_glob) {
    auto pat1 = kota::GlobPattern::create("{AAA,BBB,AB*}");
    EXPECT(pat1.has_value());
    EXPECT(pat1->match("AAA"));
    EXPECT(pat1->match("BBB"));
    EXPECT(pat1->match("AB"));
    EXPECT(pat1->match("ABCD"));
    EXPECT(!pat1->match("CCC"));
    EXPECT(pat1->match("ABCDE"));
}

ZEST_CASE(simple) {
    PATDEF(pat1, "node_modules")
    EXPECT(pat1.match("node_modules"));
    EXPECT(!pat1.match("node_module"));
    EXPECT(!pat1.match("/node_modules"));
    EXPECT(!pat1.match("test/node_modules"));

    PATDEF(pat2, "test.txt")
    EXPECT(pat2.match("test.txt"));
    EXPECT(!pat2.match("test?txt"));
    EXPECT(!pat2.match("/text.txt"));
    EXPECT(!pat2.match("test/test.txt"));

    PATDEF(pat3, "test(.txt")
    EXPECT(pat3.match("test(.txt"));
    EXPECT(!pat3.match("test?txt"));

    PATDEF(pat4, "qunit")
    EXPECT(pat4.match("qunit"));
    EXPECT(!pat4.match("qunit.css"));
    EXPECT(!pat4.match("test/qunit"));

    PATDEF(pat5, "/DNXConsoleApp/**/*.cs")
    EXPECT(pat5.match("/DNXConsoleApp/Program.cs"));
    EXPECT(pat5.match("/DNXConsoleApp/foo/Program.cs"));
}

ZEST_CASE(dot_hidden) {
    PATDEF(pat1, ".*")
    EXPECT(pat1.match(".git"));
    EXPECT(pat1.match(".hidden.txt"));
    EXPECT(!pat1.match("git"));
    EXPECT(!pat1.match("hidden.txt"));
    EXPECT(!pat1.match("path/.git"));
    EXPECT(!pat1.match("path/.hidden.txt"));

    PATDEF(pat2, "**/.*")
    EXPECT(pat2.match(".git"));
    EXPECT(pat2.match("/.git"));
    EXPECT(pat2.match(".hidden.txt"));
    EXPECT(!pat2.match("git"));
    EXPECT(!pat2.match("hidden.txt"));
    EXPECT(pat2.match("path/.git"));
    EXPECT(pat2.match("path/.hidden.txt"));
    EXPECT(pat2.match("/path/.git"));
    EXPECT(pat2.match("/path/.hidden.txt"));
    EXPECT(!pat2.match("path/git"));
    EXPECT(!pat2.match("pat.h/hidden.txt"));

    PATDEF(pat3, "._*")
    EXPECT(pat3.match("._git"));
    EXPECT(pat3.match("._hidden.txt"));
    EXPECT(!pat3.match("git"));
    EXPECT(!pat3.match("hidden.txt"));
    EXPECT(!pat3.match("path/._git"));
    EXPECT(!pat3.match("path/._hidden.txt"));

    PATDEF(pat4, "**/._*")
    EXPECT(pat4.match("._git"));
    EXPECT(pat4.match("._hidden.txt"));
    EXPECT(!pat4.match("git"));
    EXPECT(!pat4.match("hidden._txt"));
    EXPECT(pat4.match("path/._git"));
    EXPECT(pat4.match("path/._hidden.txt"));
    EXPECT(pat4.match("/path/._git"));
    EXPECT(pat4.match("/path/._hidden.txt"));
    EXPECT(!pat4.match("path/git"));
    EXPECT(!pat4.match("pat.h/hidden._txt"));
}

ZEST_CASE(escape_character) {
    PATDEF(pat1, R"(\*star)")
    EXPECT(pat1.match("*star"));

    PATDEF(pat2, R"(\{\*\})")
    EXPECT(pat2.match("{*}"));
}

ZEST_CASE(bracket_expr) {
    PATDEF(pat1, R"([a-zA-Z\]])")
    EXPECT(pat1.match(R"(])"));
    EXPECT(!pat1.match(R"([)"));
    EXPECT(pat1.match(R"(s)"));
    EXPECT(pat1.match(R"(S)"));
    EXPECT(!pat1.match(R"(0)"));

    PATDEF(pat2, R"([\\^a-zA-Z""\\])")
    EXPECT(pat2.match(R"(")"));
    EXPECT(pat2.match(R"(^)"));
    EXPECT(pat2.match(R"(\)"));
    EXPECT(pat2.match(R"(")"));
    EXPECT(pat2.match(R"(x)"));
    EXPECT(pat2.match(R"(X)"));
    EXPECT(!pat2.match(R"(0)"));

    PATDEF(pat3, R"([!0-9a-fA-F\-+\*])")
    EXPECT(!pat3.match("1"));
    EXPECT(!pat3.match("*"));
    EXPECT(pat3.match("s"));
    EXPECT(pat3.match("S"));
    EXPECT(pat3.match("H"));
    EXPECT(pat3.match("]"));

    PATDEF(pat4, R"([^\^0-9a-fA-F\-+\*])")
    EXPECT(!pat4.match("1"));
    EXPECT(!pat4.match("*"));
    EXPECT(!pat4.match("^"));
    EXPECT(pat4.match("s"));
    EXPECT(pat4.match("S"));
    EXPECT(pat4.match("H"));
    EXPECT(pat4.match("]"));

    PATDEF(pat5, R"([\*-\^])")
    EXPECT(pat5.match("*"));
    EXPECT(!pat5.match("a"));
    EXPECT(!pat5.match("z"));
    EXPECT(pat5.match("A"));
    EXPECT(pat5.match("Z"));
    EXPECT(pat5.match("\\"));
    EXPECT(pat5.match("^"));
    EXPECT(pat5.match("-"));

    PATDEF(pat6, "foo.[^0-9]")
    EXPECT(!pat6.match("foo.5"));
    EXPECT(!pat6.match("foo.8"));
    EXPECT(!pat6.match("bar.5"));
    EXPECT(pat6.match("foo.f"));

    PATDEF(pat7, "foo.[!0-9]")
    EXPECT(!pat7.match("foo.5"));
    EXPECT(!pat7.match("foo.8"));
    EXPECT(!pat7.match("bar.5"));
    EXPECT(pat7.match("foo.f"));

    PATDEF(pat8, "foo.[0!^*?]")
    EXPECT(!pat8.match("foo.5"));
    EXPECT(!pat8.match("foo.8"));
    EXPECT(pat8.match("foo.0"));
    EXPECT(pat8.match("foo.!"));
    EXPECT(pat8.match("foo.^"));
    EXPECT(pat8.match("foo.*"));
    EXPECT(pat8.match("foo.?"));

    PATDEF(pat9, "foo[/]bar")
    EXPECT(!pat9.match("foo/bar"));

    PATDEF(pat10, "foo.[[]")
    EXPECT(pat10.match("foo.["));

    PATDEF(pat11, "foo.[]]")
    EXPECT(pat11.match("foo.]"));

    PATDEF(pat12, "foo.[][!]")
    EXPECT(pat12.match("foo.]"));
    EXPECT(pat12.match("foo.["));
    EXPECT(pat12.match("foo.!"));

    PATDEF(pat13, "foo.[]-]")
    EXPECT(pat13.match("foo.]"));
    EXPECT(pat13.match("foo.-"));

    PATDEF(pat14, "foo.[0-9]")
    EXPECT(pat14.match("foo.5"));
    EXPECT(pat14.match("foo.8"));
    EXPECT(!pat14.match("bar.5"));
    EXPECT(!pat14.match("foo.f"));

    // A dash right after a completed range is a literal, not a new range
    // start chained off the previous range's end.
    PATDEF(pat15, "[a-c-e]")
    EXPECT(pat15.match("b"));
    EXPECT(pat15.match("-"));
    EXPECT(pat15.match("e"));
    EXPECT(!pat15.match("d"));
}

ZEST_CASE(brace_expr) {
    PATDEF(pat1, "*foo[0-9a-z].{c,cpp,cppm,?pp}")
    EXPECT(!pat1.match("foo1.cc"));
    EXPECT(pat1.match("foo2.cpp"));
    EXPECT(pat1.match("foo3.cppm"));
    EXPECT(pat1.match("foot.cppm"));
    EXPECT(pat1.match("foot.hpp"));
    EXPECT(pat1.match("foot.app"));
    EXPECT(!pat1.match("fooD.cppm"));
    EXPECT(!pat1.match("BarfooD.cppm"));
    EXPECT(!pat1.match("foofooD.cppm"));

    PATDEF(pat2, "proj/{build*,include,src}/*.{cc,cpp,h,hpp}")
    EXPECT(pat2.match("proj/include/foo.cc"));
    EXPECT(pat2.match("proj/include/bar.cpp"));
    EXPECT(!pat2.match("proj/include/xxx/yyy/zzz/foo.cc"));
    EXPECT(pat2.match("proj/build-yyy/foo.h"));
    EXPECT(pat2.match("proj/build-xxx/foo.cpp"));
    EXPECT(pat2.match("proj/build/foo.cpp"));
    EXPECT(!pat2.match("proj/build-xxx/xxx/yyy/zzz/foo.cpp"));

    PATDEF(pat3, "*.{html,js}")
    EXPECT(pat3.match("foo.js"));
    EXPECT(pat3.match("foo.html"));
    EXPECT(!pat3.match("folder/foo.js"));
    EXPECT(!pat3.match("/node_modules/foo.js"));
    EXPECT(!pat3.match("foo.jss"));
    EXPECT(!pat3.match("some.js/test"));

    PATDEF(pat4, "*.{html}")
    EXPECT(pat4.match("foo.html"));
    EXPECT(!pat4.match("foo.js"));
    EXPECT(!pat4.match("folder/foo.js"));
    EXPECT(!pat4.match("/node_modules/foo.js"));
    EXPECT(!pat4.match("foo.jss"));
    EXPECT(!pat4.match("some.js/test"));

    PATDEF(pat5, "{node_modules,testing}")
    EXPECT(pat5.match("node_modules"));
    EXPECT(pat5.match("testing"));
    EXPECT(!pat5.match("node_module"));
    EXPECT(!pat5.match("dtesting"));

    PATDEF(pat6, "**/{foo,bar}")
    EXPECT(pat6.match("foo"));
    EXPECT(pat6.match("bar"));
    EXPECT(pat6.match("test/foo"));
    EXPECT(pat6.match("test/bar"));
    EXPECT(pat6.match("other/more/foo"));
    EXPECT(pat6.match("other/more/bar"));
    EXPECT(pat6.match("/foo"));
    EXPECT(pat6.match("/bar"));
    EXPECT(pat6.match("/test/foo"));
    EXPECT(pat6.match("/test/bar"));
    EXPECT(pat6.match("/other/more/foo"));
    EXPECT(pat6.match("/other/more/bar"));

    PATDEF(pat7, "{foo,bar}/**")
    EXPECT(pat7.match("foo"));
    EXPECT(pat7.match("bar"));
    EXPECT(pat7.match("bar/"));
    EXPECT(pat7.match("foo/test"));
    EXPECT(pat7.match("bar/test"));
    EXPECT(pat7.match("bar/test/"));
    EXPECT(pat7.match("foo/other/more"));
    EXPECT(pat7.match("bar/other/more"));
    EXPECT(pat7.match("bar/other/more/"));

    PATDEF(pat8, "{**/*.d.ts,**/*.js}")
    EXPECT(pat8.match("foo.js"));
    EXPECT(pat8.match("testing/foo.js"));
    EXPECT(pat8.match("/testing/foo.js"));
    EXPECT(pat8.match("foo.d.ts"));
    EXPECT(pat8.match("testing/foo.d.ts"));
    EXPECT(pat8.match("/testing/foo.d.ts"));
    EXPECT(!pat8.match("foo.d"));
    EXPECT(!pat8.match("testing/foo.d"));
    EXPECT(!pat8.match("/testing/foo.d"));

    PATDEF(pat9, "{**/*.d.ts,**/*.js,path/simple.jgs}")
    EXPECT(pat9.match("foo.js"));
    EXPECT(pat9.match("testing/foo.js"));
    EXPECT(pat9.match("/testing/foo.js"));
    EXPECT(pat9.match("path/simple.jgs"));
    EXPECT(!pat9.match("/path/simple.jgs"));

    PATDEF(pat10, "{**/*.d.ts,**/*.js,foo.[0-9]}")
    EXPECT(pat10.match("foo.5"));
    EXPECT(pat10.match("foo.8"));
    EXPECT(!pat10.match("bar.5"));
    EXPECT(!pat10.match("foo.f"));
    EXPECT(pat10.match("foo.js"));

    PATDEF(pat11, "prefix/{**/*.d.ts,**/*.js,foo.[0-9]}")
    EXPECT(pat11.match("prefix/foo.5"));
    EXPECT(pat11.match("prefix/foo.8"));
    EXPECT(!pat11.match("prefix/bar.5"));
    EXPECT(!pat11.match("prefix/foo.f"));
    EXPECT(pat11.match("prefix/foo.js"));

    // A bare single-star alternative remains segment-bounded.
    PATDEF(pat12, "{*,foo}")
    EXPECT(pat12.match("foo"));
    EXPECT(pat12.match("bar"));
    EXPECT(!pat12.match("a/b"));
    EXPECT(!pat12.match("/foo"));

    PATDEF(pat13, "{foo,**}")
    EXPECT(pat13.match("a/b/c"));

    // With a prefix the star arm is `a*`: segment-bounded as usual.
    PATDEF(pat14, "a{*,foo}")
    EXPECT(pat14.match("ab"));
    EXPECT(pat14.match("afoo"));
    EXPECT(!pat14.match("a/b"));
}

ZEST_CASE(globstar_prefix) {
    // **/* — match any path
    PATDEF(pat1, "**/*")
    EXPECT(pat1.match("foo"));
    EXPECT(pat1.match("foo/bar"));
    EXPECT(pat1.match("foo/bar/baz"));

    // **/[0-9]* — last segment starts with digit
    PATDEF(pat2, "**/[0-9]*")
    EXPECT(pat2.match("114514foo"));
    EXPECT(!pat2.match("foo/bar/baz/xxx/yyy/zzz"));
    EXPECT(!pat2.match("foo/bar/baz/xxx/yyy/zzz114514"));
    EXPECT(pat2.match("foo/bar/baz/xxx/yyy/114514"));
    EXPECT(pat2.match("foo/bar/baz/xxx/yyy/114514zzz"));

    // **/*[0-9] — last segment ends with digit
    PATDEF(pat3, "**/*[0-9]")
    EXPECT(pat3.match("foo5"));
    EXPECT(!pat3.match("foo/bar/baz/xxx/yyy/zzz"));
    EXPECT(pat3.match("foo/bar/baz/xxx/yyy/zzz114514"));

    // **/include/test/*.{cc,...} — globstar prefix with multi-segment literal
    PATDEF(pat4, "**/include/test/*.{cc,hh,c,h,cpp,hpp}")
    EXPECT(pat4.match("include/test/aaa.cc"));
    EXPECT(pat4.match("/include/test/aaa.cc"));
    EXPECT(pat4.match("xxx/yyy/include/test/aaa.cc"));
    EXPECT(pat4.match("include/foo/bar/baz/include/test/bbb.hh"));
    EXPECT(pat4.match("include/include/include/include/include/test/bbb.hpp"));

    // Embedded ** is a single-segment wildcard, as in VS Code and Git.
    PATDEF(pat5, "**include/test/*.{cc,hh,c,h,cpp,hpp}")
    EXPECT(pat5.match("include/test/fff.hpp"));
    EXPECT(pat5.match("xxx-yyy-include/test/fff.hpp"));
    EXPECT(pat5.match("xxx-yyy-include/test/.hpp"));
    EXPECT(!pat5.match("/include/test/aaa.cc"));
    EXPECT(!pat5.match("include/foo/bar/baz/include/test/bbb.hh"));

    // **/*foo.{c,cpp} — globstar prefix with wildcard suffix
    PATDEF(pat6, "**/*foo.{c,cpp}")
    EXPECT(pat6.match("bar/foo.cpp"));
    EXPECT(pat6.match("bar/barfoo.cpp"));
    EXPECT(pat6.match("/foofoo.cpp"));
    EXPECT(pat6.match("foo/foo/foo/foo/foofoo.cpp"));
    EXPECT(pat6.match("foofoo.cpp"));
    EXPECT(pat6.match("barfoo.cpp"));
    EXPECT(pat6.match("foo.cpp"));

    // ** — matches everything
    PATDEF(pat7, "**")
    EXPECT(pat7.match("foo"));
    EXPECT(pat7.match("foo/bar/baz"));
    EXPECT(pat7.match("/"));
    EXPECT(pat7.match("foo.js"));
    EXPECT(pat7.match("folder/foo.js"));
    EXPECT(pat7.match("folder/foo/"));
    EXPECT(pat7.match("/node_modules/foo.js"));
    EXPECT(pat7.match("foo.jss"));
    EXPECT(pat7.match("some.js/test"));

    // **/x — match literal at any depth
    PATDEF(pat8, "**/x")
    EXPECT(pat8.match("x"));
    EXPECT(pat8.match("/x"));
    EXPECT(pat8.match("/x/x/x/x/x"));

    // **/*.{cc,cpp} — extension match at any depth
    PATDEF(pat9, "**/*.{cc,cpp}")
    EXPECT(pat9.match("foo/bar/baz.cc"));
    EXPECT(pat9.match("foo/foo/foo.cpp"));
    EXPECT(pat9.match("foo/bar/.cc"));

    // **/*?.{cc,cpp} — wildcard then question before extension
    PATDEF(pat10, "**/*?.{cc,cpp}")
    EXPECT(pat10.match("foo/bar/baz/xxx/yyy/zzz/aaa.cc"));
    EXPECT(pat10.match("foo/bar/baz/xxx/yyy/zzz/a.cc"));
    EXPECT(!pat10.match("foo/bar/baz/xxx/yyy/zzz/.cc"));

    // **/?*.{cc,cpp} — question then wildcard before extension
    PATDEF(pat11, "**/?*.{cc,cpp}")
    EXPECT(pat11.match("foo/bar/baz/xxx/yyy/zzz/aaa.cc"));
    EXPECT(pat11.match("foo/bar/baz/xxx/yyy/zzz/a.cc"));
    EXPECT(!pat11.match("foo/bar/baz/xxx/yyy/zzz/.cc"));

    // **/*.js — JS file at any depth
    PATDEF(pat12, "**/*.js")
    EXPECT(pat12.match("foo.js"));
    EXPECT(pat12.match("/foo.js"));
    EXPECT(pat12.match("folder/foo.js"));
    EXPECT(pat12.match("/node_modules/foo.js"));
    EXPECT(!pat12.match("foo.jss"));
    EXPECT(!pat12.match("some.js/test"));
    EXPECT(!pat12.match("/some.js/test"));

    // **/project.json — exact filename at any depth
    PATDEF(pat13, "**/project.json")
    EXPECT(pat13.match("project.json"));
    EXPECT(pat13.match("/project.json"));
    EXPECT(pat13.match("some/folder/project.json"));
    EXPECT(pat13.match("/some/folder/project.json"));
    EXPECT(!pat13.match("some/folder/file_project.json"));
    EXPECT(!pat13.match("some/folder/fileproject.json"));
    EXPECT(!pat13.match("some/rrproject.json"));
}

ZEST_CASE(globstar_suffix) {
    // x/** — everything under x/
    PATDEF(pat1, "x/**")
    EXPECT(pat1.match("x/"));
    EXPECT(pat1.match("x/foo/bar/baz"));
    EXPECT(pat1.match("x"));

    // test/** — everything under test/
    PATDEF(pat2, "test/**")
    EXPECT(pat2.match("test"));
    EXPECT(pat2.match("test/foo"));
    EXPECT(pat2.match("test/foo/"));
    EXPECT(pat2.match("test/foo.js"));
    EXPECT(pat2.match("test/other/foo.js"));
    EXPECT(!pat2.match("est/other/foo.js"));
}

ZEST_CASE(globstar_middle) {
    // test/**/*.js — JS files under test/ at any depth
    PATDEF(pat1, "test/**/*.js")
    EXPECT(pat1.match("test/foo.js"));
    EXPECT(pat1.match("test/other/foo.js"));
    EXPECT(pat1.match("test/other/more/foo.js"));
    EXPECT(!pat1.match("test/foo.ts"));
    EXPECT(!pat1.match("test/other/foo.ts"));
    EXPECT(!pat1.match("test/other/more/foo.ts"));

    // some/**/*.js — JS files under some/ at any depth
    PATDEF(pat2, "some/**/*.js")
    EXPECT(pat2.match("some/foo.js"));
    EXPECT(pat2.match("some/folder/foo.js"));
    EXPECT(!pat2.match("something/foo.js"));
    EXPECT(!pat2.match("something/folder/foo.js"));

    // some/**/* — any file under some/ at any depth
    PATDEF(pat3, "some/**/*")
    EXPECT(pat3.match("some/foo.js"));
    EXPECT(pat3.match("some/folder/foo.js"));
    EXPECT(!pat3.match("something/foo.js"));
    EXPECT(!pat3.match("something/folder/foo.js"));
}

ZEST_CASE(globstar_complex) {
    // **/**/*.js — double globstar
    PATDEF(pat1, "**/**/*.js")
    EXPECT(pat1.match("foo.js"));
    EXPECT(pat1.match("/foo.js"));
    EXPECT(pat1.match("folder/foo.js"));
    EXPECT(pat1.match("/node_modules/foo.js"));
    EXPECT(!pat1.match("foo.jss"));
    EXPECT(!pat1.match("some.js/test"));

    // **/node_modules/**/*.js — scoped to node_modules
    PATDEF(pat2, "**/node_modules/**/*.js")
    EXPECT(!pat2.match("foo.js"));
    EXPECT(!pat2.match("folder/foo.js"));
    EXPECT(pat2.match("node_modules/foo.js"));
    EXPECT(pat2.match("/node_modules/foo.js"));
    EXPECT(pat2.match("node_modules/some/folder/foo.js"));
    EXPECT(pat2.match("/node_modules/some/folder/foo.js"));
    EXPECT(!pat2.match("node_modules/some/folder/foo.ts"));
    EXPECT(!pat2.match("foo.jss"));
    EXPECT(!pat2.match("some.js/test"));

    // Brace with multiple globstar patterns
    PATDEF(pat3, "{**/node_modules/**,**/.git/**,**/bower_components/**}")
    EXPECT(pat3.match("node_modules"));
    EXPECT(pat3.match("/node_modules"));
    EXPECT(pat3.match("/node_modules/more"));
    EXPECT(pat3.match("some/test/node_modules"));
    EXPECT(pat3.match("/some/test/node_modules"));
    EXPECT(pat3.match("bower_components"));
    EXPECT(pat3.match("bower_components/more"));
    EXPECT(pat3.match("/bower_components"));
    EXPECT(pat3.match("some/test/bower_components"));
    EXPECT(pat3.match("/some/test/bower_components"));
    EXPECT(pat3.match(".git"));
    EXPECT(pat3.match("/.git"));
    EXPECT(pat3.match("some/test/.git"));
    EXPECT(pat3.match("/some/test/.git"));
    EXPECT(!pat3.match("tempting"));
    EXPECT(!pat3.match("/tempting"));
    EXPECT(!pat3.match("some/test/tempting"));
    EXPECT(!pat3.match("/some/test/tempting"));

    // Brace with multiple globstar-prefixed patterns
    PATDEF(pat4, "{**/package.json,**/project.json}")
    EXPECT(pat4.match("package.json"));
    EXPECT(pat4.match("/package.json"));
    EXPECT(!pat4.match("xpackage.json"));
    EXPECT(!pat4.match("/xpackage.json"));
}

ZEST_CASE(error_paths) {
    // Unmatched '['
    auto e1 = kota::GlobPattern::create("foo.[a-z");
    EXPECT(!e1.has_value());

    // '[' as last character
    auto e2 = kota::GlobPattern::create("{a,[}");
    EXPECT(!e2.has_value());

    // Stray '\' at end of pattern
    auto e3 = kota::GlobPattern::create("foo\\");
    EXPECT(!e3.has_value());

    // Stray '\' at end inside brace
    auto e4 = kota::GlobPattern::create("{foo\\}");
    EXPECT(!e4.has_value());

    // Stray '\' inside bracket inside brace
    auto e5 = kota::GlobPattern::create("{[abc\\]}");
    EXPECT(!e5.has_value());

    // Empty brace expression {}
    auto e6 = kota::GlobPattern::create("foo.{}");
    EXPECT(!e6.has_value());

    // Nested braces
    auto e7 = kota::GlobPattern::create("{a,{b,c}}");
    EXPECT(!e7.has_value());

    // Incomplete brace expansion (unmatched '{')
    auto e8 = kota::GlobPattern::create("{foo,bar");
    EXPECT(!e8.has_value());

    // *** (triple star)
    auto e9 = kota::GlobPattern::create("***.js");
    EXPECT(!e9.has_value());

    // ** is valid (boundary)
    auto e10 = kota::GlobPattern::create("**.js");
    EXPECT(e10.has_value());

    // Multiple consecutive slashes in literal pattern
    auto e11 = kota::GlobPattern::create("foo//bar");
    EXPECT(!e11.has_value());

    // Multiple consecutive slashes at start
    auto e12 = kota::GlobPattern::create("//foo");
    EXPECT(!e12.has_value());

    // Multiple consecutive slashes after the literal prefix
    auto e13 = kota::GlobPattern::create("**/foo//*.cc");
    EXPECT(!e13.has_value());

    // Unmatched '[' after a wildcard
    auto e14 = kota::GlobPattern::create("*[");
    EXPECT(!e14.has_value());

    // '\' at end inside bracket inside brace
    auto e15 = kota::GlobPattern::create("{[\\]}");
    EXPECT(!e15.has_value());

    // Range start > end
    auto e16 = kota::GlobPattern::create("[z-a]");
    EXPECT(!e16.has_value());

    // Range end is stray backslash
    auto e17 = kota::GlobPattern::create("[a-\\]");
    EXPECT(!e17.has_value());

    // Brace expansion is textual: an arm starting with `/` right after the
    // literal prefix's separator spells `//`, escaped prefix or not.
    for(std::string_view source: {"a/{/}", "a/{/b}", "a/{b,/c}", R"(a\*/{/})"}) {
        auto res = kota::GlobPattern::create(source);
        EXPECT(!res.has_value());
        if(!res.has_value()) {
            EXPECT(res.error().kind == kota::GlobError::MultipleSlash);
        }
    }
    EXPECT(kota::GlobPattern::create("{/a}")->match("/a"));
    EXPECT(kota::GlobPattern::create("a{/b}")->match("a/b"));
}

ZEST_CASE(empty_and_trivial) {
    // Empty pattern matches only empty string
    PATDEF(pat1, "")
    EXPECT(pat1.match(""));
    EXPECT(!pat1.match("foo"));
    EXPECT(!pat1.match("/"));

    // Single character pattern
    PATDEF(pat2, "a")
    EXPECT(pat2.match("a"));
    EXPECT(!pat2.match("b"));
    EXPECT(!pat2.match("ab"));
    EXPECT(!pat2.match(""));

    // Slash-only pattern
    PATDEF(pat3, "/")
    EXPECT(pat3.match("/"));
    EXPECT(!pat3.match(""));
    EXPECT(!pat3.match("//"));

    // Literal path with slashes (was rejected before bug fix)
    PATDEF(pat4, "foo/bar")
    EXPECT(pat4.match("foo/bar"));
    EXPECT(!pat4.match("foo/baz"));
    EXPECT(!pat4.match("foo/bar/baz"));
    EXPECT(!pat4.match("foobar"));

    // Literal multi-segment path
    PATDEF(pat5, "a/b/c/d")
    EXPECT(pat5.match("a/b/c/d"));
    EXPECT(!pat5.match("a/b/c"));
    EXPECT(!pat5.match("a/b/c/d/e"));
}

ZEST_CASE(is_trivial_match_all) {
    auto p1 = kota::GlobPattern::create("**");
    EXPECT(p1.has_value());
    EXPECT(p1->is_trivial_match_all());

    auto p2 = kota::GlobPattern::create("*");
    EXPECT(p2.has_value());
    EXPECT(!p2->is_trivial_match_all());

    auto p3 = kota::GlobPattern::create("**/*");
    EXPECT(p3.has_value());
    EXPECT(!p3->is_trivial_match_all());

    auto p4 = kota::GlobPattern::create("foo/**");
    EXPECT(p4.has_value());
    EXPECT(!p4->is_trivial_match_all());

    auto p5 = kota::GlobPattern::create("*.js");
    EXPECT(p5.has_value());
    EXPECT(!p5->is_trivial_match_all());

    auto p6 = kota::GlobPattern::create("{a,b}");
    EXPECT(p6.has_value());
    EXPECT(!p6->is_trivial_match_all());

    // The leading `/` is a real constraint even though the literal prefix
    // it leaves behind is empty.
    auto p7 = kota::GlobPattern::create("/*");
    EXPECT(p7.has_value());
    EXPECT(!p7->is_trivial_match_all());

    auto p8 = kota::GlobPattern::create("/**");
    EXPECT(p8.has_value());
    EXPECT(!p8->is_trivial_match_all());

    // Only a whole-segment ** brace arm is match-all.
    auto p9 = kota::GlobPattern::create("{*,foo}");
    EXPECT(p9.has_value());
    EXPECT(!p9->is_trivial_match_all());

    auto p10 = kota::GlobPattern::create("{foo,**}");
    EXPECT(p10.has_value());
    EXPECT(p10->is_trivial_match_all());

    // With a prefix the `*` arm means `a*`, which is segment-bounded.
    auto p11 = kota::GlobPattern::create("a{*,foo}");
    EXPECT(p11.has_value());
    EXPECT(!p11->is_trivial_match_all());

    auto p12 = kota::GlobPattern::create("{*.js,foo}");
    EXPECT(p12.has_value());
    EXPECT(!p12->is_trivial_match_all());
}

ZEST_CASE(single_star) {
    PATDEF(pat1, "*")
    EXPECT(pat1.match("foo"));
    EXPECT(pat1.match("bar.txt"));
    EXPECT(pat1.match("a"));
    // Like VS Code, a standalone single star is segment-bounded.
    EXPECT(!pat1.match("foo/bar"));
    EXPECT(!pat1.match("/foo"));

    // * in a segment
    PATDEF(pat2, "*/b")
    EXPECT(pat2.match("a/b"));
    EXPECT(pat2.match("foo/b"));
    EXPECT(!pat2.match("a/c"));
    EXPECT(!pat2.match("a/b/c"));

    // ? in a segment
    PATDEF(pat3, "?/b")
    EXPECT(pat3.match("a/b"));
    EXPECT(pat3.match("x/b"));
    EXPECT(!pat3.match("ab/b"));
    EXPECT(!pat3.match("/b"));
}

ZEST_CASE(star_stays_in_segment) {
    // Backtracking must not let a single * swallow a `/`.
    PATDEF(pat1, "**/a*.cc")
    EXPECT(pat1.match("x/ab.cc"));
    EXPECT(pat1.match("x/y/a.cc"));
    EXPECT(!pat1.match("x/a/b.cc"));
    EXPECT(!pat1.match("a/b.cc"));

    PATDEF(pat2, "?*.cc")
    EXPECT(pat2.match("ab.cc"));
    EXPECT(!pat2.match("a/.cc"));
    EXPECT(!pat2.match("a/b.cc"));

    // A terminal single star is bounded by its segment.
    PATDEF(pat3, "a*")
    EXPECT(pat3.match("abc"));
    EXPECT(!pat3.match("abc/"));
    EXPECT(!pat3.match("abc/d"));

    PATDEF(pat4, "foo/*")
    EXPECT(pat4.match("foo/bar"));
    EXPECT(!pat4.match("foo/a/b"));
}

ZEST_CASE(segment_boundary) {
    // The input segment must end exactly where the pattern segment does; a
    // mismatch there backtracks into an earlier star instead of aborting.
    PATDEF(pat1, "*a/b")
    EXPECT(pat1.match("aa/b"));
    EXPECT(pat1.match("xya/b"));
    EXPECT(!pat1.match("ab/b"));

    // One pattern `/` consumes exactly one input `/`.
    PATDEF(pat2, "?/b")
    EXPECT(!pat2.match("a//b"));

    PATDEF(pat3, "*/b")
    EXPECT(!pat3.match("a//b"));

    // A segment following `**` matches whole input segments; ** absorbs
    // full segments only.
    PATDEF(pat4, "**/a/b")
    EXPECT(pat4.match("a/b"));
    EXPECT(pat4.match("q/a/b"));
    EXPECT(!pat4.match("aX/b"));
    EXPECT(!pat4.match("q/aX/b"));

    PATDEF(pat5, "**/?x")
    EXPECT(pat5.match("ax"));
    EXPECT(pat5.match("aa/ax"));
    EXPECT(!pat5.match("aax"));
}

ZEST_CASE(matches_empty_tail) {
    // Star runs match empty input; a `/` does so only when absorbed by an
    // adjacent globstar.
    PATDEF(pat1, "foo/**")
    EXPECT(pat1.match("foo"));
    EXPECT(pat1.match("foo/a/b"));

    PATDEF(pat2, "foo/*")
    EXPECT(!pat2.match("foo"));

    PATDEF(pat3, "*/*")
    EXPECT(!pat3.match(""));
    EXPECT(pat3.match("a/b"));

    PATDEF(pat4, "?/*")
    EXPECT(!pat4.match("a"));
    EXPECT(pat4.match("a/"));

    PATDEF(pat5, "**/*")
    EXPECT(pat5.match(""));

    PATDEF(pat6, "x*/**")
    EXPECT(pat6.match("x"));

    PATDEF(pat7, "x*/*")
    EXPECT(!pat7.match("x"));

    PATDEF(pat8, "foo/{,x}")
    EXPECT(!pat8.match("foo"));
    EXPECT(pat8.match("foo/"));
    EXPECT(pat8.match("foo/x"));

    // Each globstar absorbs at most one separator, so a `/**/` between two
    // required segments still demands a real `/`.
    PATDEF(pat9, "x*/**/*")
    EXPECT(!pat9.match("x"));
    EXPECT(pat9.match("x/y"));

    PATDEF(pat10, "*a/**/*")
    EXPECT(!pat10.match("aa"));
    EXPECT(pat10.match("aa/b"));

    // A trailing globstar chain absorbs the separator after the prefix; any
    // non-globstar atom in the tail still demands real input.
    PATDEF(pat11, "foo/**/*")
    EXPECT(!pat11.match("foo"));
    EXPECT(pat11.match("foo/a"));

    PATDEF(pat12, "foo/**/bar")
    EXPECT(!pat12.match("foo"));
    EXPECT(pat12.match("foo/bar"));

    // Prefix stripping must not change matches: `a/**/**` behaves exactly
    // like its brace-wrapped twin, whose arm keeps the leading literal.
    PATDEF(pat13, "a/**/**")
    EXPECT(pat13.match("a"));
    EXPECT(pat13.match("a/b"));

    PATDEF(pat13b, "{a/**/**}")
    EXPECT(pat13b.match("a"));
    EXPECT(pat13b.match("a/b"));

    // Brace arms expand into independent alternatives, so the `**` arm
    // absorbs the separator while the literal arm does not.
    PATDEF(pat14, "foo/{**,x}")
    EXPECT(pat14.match("foo"));
    EXPECT(pat14.match("foo/x"));

    // ** may match zero segments, letting later stars take the empty
    // segments around a bare `/`.
    PATDEF(pat15, "**/*/*")
    EXPECT(pat15.match("/"));
    EXPECT(pat15.match("a/b"));
    EXPECT(!pat15.match(""));

    PATDEF(pat16, "x/**/*/*")
    EXPECT(pat16.match("x//"));
    EXPECT(pat16.match("x/a/b"));
    EXPECT(!pat16.match("x/a"));
}

ZEST_CASE(single_question) {
    PATDEF(pat1, "?")
    EXPECT(pat1.match("a"));
    EXPECT(pat1.match("z"));
    EXPECT(pat1.match("0"));
    EXPECT(!pat1.match(""));
    EXPECT(!pat1.match("ab"));
    EXPECT(!pat1.match("/"));

    PATDEF(pat2, "??")
    EXPECT(pat2.match("ab"));
    EXPECT(pat2.match("12"));
    EXPECT(!pat2.match("a"));
    EXPECT(!pat2.match("abc"));

    PATDEF(pat3, "?.?")
    EXPECT(pat3.match("a.b"));
    EXPECT(!pat3.match("ab.c"));
    EXPECT(!pat3.match("a.bc"));
}

ZEST_CASE(star_matches_zero) {
    // * matches zero or more chars (doc says "zero or more")
    PATDEF(pat1, "*.cc")
    EXPECT(pat1.match(".cc"));
    EXPECT(pat1.match("foo.cc"));
    EXPECT(!pat1.match("foo.cpp"));

    PATDEF(pat2, "*foo")
    EXPECT(pat2.match("foo"));
    EXPECT(pat2.match("barfoo"));
}

ZEST_CASE(trailing_slash) {
    // Pattern "foo/" — prefix is "foo/", no sub_globs
    // match("foo/") should be true (exact match)
    // match("foo") should be false (prefix doesn't match)
    PATDEF(pat1, "foo/")
    EXPECT(pat1.match("foo/"));
    EXPECT(!pat1.match("foo"));
    EXPECT(!pat1.match("foo/bar"));
}

ZEST_CASE(boundary_edge_cases) {
    // A leading ] is a literal member even after the negation operator.
    PATDEF(pat1, "[^]]")
    EXPECT(pat1.match("a"));
    EXPECT(pat1.match("0"));
    EXPECT(!pat1.match("a]"));
    EXPECT(!pat1.match("]"));
    EXPECT(!pat1.match("/]"));

    // {,a} — empty alternative in brace
    PATDEF(pat2, "{,a}")
    EXPECT(pat2.match("a"));
    EXPECT(pat2.match(""));

    // {a\,b,c} — escaped comma treated as literal inside brace
    // The brace parser sees \ and skips next char, so {a\,b,c} has terms: "a\,b" and "c"
    // The arm parser then sees "a\,b" and treats \, as escaped comma
    PATDEF(pat3, R"({a\,b,c})")
    EXPECT(pat3.match("a,b"));
    EXPECT(pat3.match("c"));
    EXPECT(!pat3.match("a"));
    EXPECT(!pat3.match("b"));
}

ZEST_CASE(match_empty_string) {
    // ** matches any number of path segments including none
    PATDEF(pat1, "**")
    EXPECT(pat1.match(""));

    PATDEF(pat2, "*")
    EXPECT(pat2.match(""));

    PATDEF(pat3, "foo")
    EXPECT(!pat3.match(""));

    PATDEF(pat4, "*.js")
    EXPECT(!pat4.match(""));
}

ZEST_CASE(inverted_bracket) {
    PATDEF(pat1, "[!a]")
    EXPECT(pat1.match("b"));
    EXPECT(pat1.match("z"));
    EXPECT(pat1.match("0"));
    EXPECT(!pat1.match("a"));
    // Critical: inverted bracket must NOT match '/'
    EXPECT(!pat1.match("/"));

    PATDEF(pat2, "[!0-9]")
    EXPECT(pat2.match("a"));
    EXPECT(!pat2.match("5"));
    EXPECT(!pat2.match("/"));

    PATDEF(pat3, "[^a-z]")
    EXPECT(pat3.match("0"));
    EXPECT(pat3.match("A"));
    EXPECT(!pat3.match("a"));
    EXPECT(!pat3.match("/"));
}

ZEST_CASE(multiple_globstar) {
    PATDEF(pat1, "**/foo/**/bar")
    EXPECT(pat1.match("foo/bar"));
    EXPECT(pat1.match("a/foo/b/bar"));
    EXPECT(pat1.match("a/b/foo/c/d/e/bar"));
    EXPECT(pat1.match("/foo/bar"));
    EXPECT(pat1.match("x/y/foo/z/bar"));
    EXPECT(!pat1.match("a/b/bar"));
    EXPECT(!pat1.match("foo/baz"));
    EXPECT(!pat1.match("foobar"));

    PATDEF(pat2, "**/a/**/b/**/c")
    EXPECT(pat2.match("a/b/c"));
    EXPECT(pat2.match("x/a/y/b/z/c"));
    EXPECT(!pat2.match("a/c"));
    EXPECT(!pat2.match("a/b"));
}

ZEST_CASE(max_subpattern_limit) {
    // {a,b} x {c,d} = 4 subpatterns, limit 2 => fail
    auto p1 = kota::GlobPattern::create("{a,b}.{c,d}", 2);
    EXPECT(!p1.has_value());

    // Same with limit 4 => succeed
    auto p2 = kota::GlobPattern::create("{a,b}.{c,d}", 4);
    EXPECT(p2.has_value());

    // Single brace with 3 terms, limit 2 => fail
    auto p3 = kota::GlobPattern::create("{a,b,c}", 2);
    EXPECT(!p3.has_value());

    // Limit 0 disables brace expansion, pattern kept as literal with braces
    auto p4 = kota::GlobPattern::create("{a,b}", 0);
    EXPECT(p4.has_value());
    EXPECT(p4->match("{a,b}"));
    EXPECT(!p4->match("a"));
    EXPECT(!p4->match("b"));

    // Limit 1 means only 1 subpattern allowed; single brace with 1 term is OK
    auto p5 = kota::GlobPattern::create("{a}", 1);
    EXPECT(p5.has_value());
    EXPECT(p5->match("a"));
}

ZEST_CASE(bracket_at_start) {
    PATDEF(pat1, "[a-z]oo")
    EXPECT(pat1.match("foo"));
    EXPECT(pat1.match("boo"));
    EXPECT(!pat1.match("Foo"));
    EXPECT(!pat1.match("1oo"));
    EXPECT(!pat1.match("aoo/bar"));

    PATDEF(pat2, "[0-9]*")
    EXPECT(pat2.match("1foo"));
    EXPECT(pat2.match("9"));
    EXPECT(!pat2.match("a1"));
}

ZEST_CASE(bracket_in_brace) {
    PATDEF(pat1, "{[a-z]oo,[0-9]ar}")
    EXPECT(pat1.match("foo"));
    EXPECT(pat1.match("boo"));
    EXPECT(pat1.match("1ar"));
    EXPECT(pat1.match("9ar"));
    EXPECT(!pat1.match("Foo"));
    EXPECT(!pat1.match("bar"));

    // Bracket with special chars inside brace
    PATDEF(pat2, R"({foo.[\*\?],bar})")
    EXPECT(pat2.match("foo.*"));
    EXPECT(pat2.match("foo.?"));
    EXPECT(pat2.match("bar"));
    EXPECT(!pat2.match("foo.x"));
}

ZEST_CASE(question_with_globstar) {
    PATDEF(pat1, "**/?.js")
    EXPECT(pat1.match("a.js"));
    EXPECT(pat1.match("foo/b.js"));
    EXPECT(pat1.match("a/b/c.js"));
    EXPECT(!pat1.match("ab.js"));
    EXPECT(!pat1.match("foo/ab.js"));
    EXPECT(!pat1.match(".js"));

    PATDEF(pat2, "**/?")
    EXPECT(pat2.match("a"));
    EXPECT(pat2.match("foo/a"));
    EXPECT(pat2.match("a/b/c/d"));
    // ** absorbs whole segments only, so the two-char basename 'ab' does not
    // match: ? must start at a segment boundary like every other atom.
    EXPECT(!pat2.match("ab"));
    EXPECT(!pat2.match("foo/ab"));
}

ZEST_CASE(prefix_strip_boundary) {
    // Stripping the literal prefix must not turn a mid-segment position
    // into a segment boundary: each pattern behaves exactly like its
    // brace-wrapped twin, whose arm keeps the leading literal.
    PATDEF(pat1, "a**/?")
    EXPECT(!pat1.match("aa"));
    EXPECT(!pat1.match("ab"));
    EXPECT(!pat1.match("a"));
    EXPECT(pat1.match("a/c"));
    EXPECT(pat1.match("aa/c"));

    PATDEF(pat1b, "{a**/?}")
    EXPECT(!pat1b.match("aa"));
    EXPECT(!pat1b.match("ab"));
    EXPECT(!pat1b.match("a"));
    EXPECT(pat1b.match("a/c"));
    EXPECT(pat1b.match("aa/c"));

    PATDEF(pat2, "x**/y")
    EXPECT(!pat2.match("xy"));
    EXPECT(pat2.match("x/y"));
    EXPECT(pat2.match("xx/y"));

    PATDEF(pat3, "a**/[bc]")
    EXPECT(!pat3.match("ab"));
    EXPECT(pat3.match("a/b"));

    PATDEF(pat4, R"(a**/\?)")
    EXPECT(!pat4.match("a?"));
    EXPECT(pat4.match("a/?"));

    // Pattern segment 0 continues the prefix's input segment, so
    // mid-segment continuation there stays legal.
    PATDEF(pat5, "a?")
    EXPECT(pat5.match("ab"));
    EXPECT(!pat5.match("a/"));

    PATDEF(pat6, "a[bc]")
    EXPECT(pat6.match("ab"));
}

ZEST_CASE(globstar_slash) {
    PATDEF(pat1, "**/")
    EXPECT(pat1.match("foo/bar"));
    EXPECT(pat1.match("foo"));
    EXPECT(pat1.match("/"));

    // ** followed by literal after /
    PATDEF(pat2, "**/x")
    EXPECT(pat2.match("x"));
    EXPECT(pat2.match("/x"));
    EXPECT(pat2.match("a/b/c/x"));
    EXPECT(!pat2.match("ax"));
    EXPECT(!pat2.match("a/bx"));

    // A leading `/` must be matched by the input.
    PATDEF(pat3, "/*")
    EXPECT(pat3.match("/foo"));
    EXPECT(pat3.match("/"));
    EXPECT(!pat3.match("foo"));
    EXPECT(!pat3.match("foo/bar"));
    EXPECT(!pat3.match("/foo/bar"));

    PATDEF(pat4, "/**")
    EXPECT(pat4.match("/foo"));
    EXPECT(pat4.match("/foo/bar"));
    EXPECT(!pat4.match("foo"));
    EXPECT(!pat4.match("foo/bar"));

    // A trailing `/` requires the input to end at that segment boundary; a
    // preceding `**` keeps retrying until the last segment lines up.
    PATDEF(pat5, "**/a/")
    EXPECT(pat5.match("a/"));
    EXPECT(pat5.match("x/a/"));
    EXPECT(pat5.match("a/a/"));
    EXPECT(pat5.match("a/b/a/"));
    EXPECT(!pat5.match("a"));
    EXPECT(!pat5.match("a/b/"));
    EXPECT(!pat5.match("a/a"));
}

ZEST_CASE(backslash_in_input) {
    // ? should match a single backslash in the input
    PATDEF(pat1, "?")
    EXPECT(pat1.match("\\"));

    // * should match strings containing backslashes
    PATDEF(pat2, "*")
    EXPECT(pat2.match("a\\b"));
    EXPECT(pat2.match("\\"));

    // Literal match with no special meaning of \ in input
    PATDEF(pat3, "**/*.txt")
    EXPECT(pat3.match("path\\with\\backslash.txt"));
}

ZEST_CASE(globstar_intermediate) {
    PATDEF(pat1, "**/*/foo")
    EXPECT(pat1.match("a/foo"));
    EXPECT(pat1.match("x/y/a/foo"));
    EXPECT(!pat1.match("foo"));

    PATDEF(pat2, "**/*.js/**/test")
    EXPECT(pat2.match("foo.js/test"));
    EXPECT(pat2.match("foo.js/a/b/test"));
    EXPECT(pat2.match("a/foo.js/test"));
    EXPECT(!pat2.match("foo/test"));
}

ZEST_CASE(unicode_question) {
    PATDEF(pat1, "?文.txt")
    EXPECT(pat1.match("中文.txt"));
    EXPECT(!pat1.match("文.txt"));
    EXPECT(!pat1.match("中中文.txt"));

    PATDEF(pat2, "?.txt")
    EXPECT(pat2.match("中.txt"));
    EXPECT(pat2.match("🚀.txt"));
    EXPECT(!pat2.match("中文.txt"));

    PATDEF(pat3, "??.txt")
    EXPECT(pat3.match("中文.txt"));
    EXPECT(!pat3.match("中.txt"));
}

ZEST_CASE(unicode_bracket) {
    PATDEF(pat1, "[中文].txt")
    EXPECT(pat1.match("中.txt"));
    EXPECT(pat1.match("文.txt"));
    EXPECT(!pat1.match("英.txt"));
    EXPECT(!pat1.match("a.txt"));

    // 中 (U+4E2D) lies inside the range 一 (U+4E00) .. 十 (U+5341).
    PATDEF(pat2, "[一-十].txt")
    EXPECT(pat2.match("中.txt"));
    EXPECT(!pat2.match("a.txt"));

    PATDEF(pat3, "[!一-十].txt")
    EXPECT(!pat3.match("中.txt"));
    EXPECT(pat3.match("a.txt"));

    PATDEF(pat4, "[a-z中]?")
    EXPECT(pat4.match("中文"));
    EXPECT(pat4.match("x文"));
    EXPECT(!pat4.match("文文"));

    // α-γ then a literal dash then ε: the dash after a completed range
    // stays literal for multi-byte members too.
    PATDEF(pat5, "[α-γ-ε]")
    EXPECT(pat5.match("β"));
    EXPECT(pat5.match("-"));
    EXPECT(pat5.match("ε"));
    EXPECT(!pat5.match("δ"));
}

ZEST_CASE(unicode_escape_star) {
    PATDEF(pat1, "\\中.txt")
    EXPECT(pat1.match("中.txt"));
    EXPECT(!pat1.match("文.txt"));

    PATDEF(pat2, "*文.txt")
    EXPECT(pat2.match("中文.txt"));
    EXPECT(pat2.match("文.txt"));
    EXPECT(!pat2.match("中英.txt"));

    PATDEF(pat3, "**/中.txt")
    EXPECT(pat3.match("a/b/中.txt"));
    EXPECT(pat3.match("中.txt"));
}

ZEST_CASE(unicode_segments) {
    PATDEF(pat1, "中/文.txt")
    EXPECT(pat1.match("中/文.txt"));
    EXPECT(!pat1.match("中文.txt"));

    PATDEF(pat2, "*/文*")
    EXPECT(pat2.match("中/文x"));
    EXPECT(!pat2.match("中/x文"));

    // The stripped literal prefix ends mid-segment; segment 0 continues it.
    PATDEF(pat3, "中?")
    EXPECT(pat3.match("中文"));
    EXPECT(!pat3.match("x文"));
}

ZEST_CASE(star_atom_alignment) {
    // A star retry must extend by whole characters. Byte-wise stepping
    // would stop inside 中 and let `?`/`[!X]` consume its continuation
    // bytes as two extra characters, wrongly matching two-character
    // inputs against three-atom tails.
    PATDEF(pat1, "*?[!X]X")
    EXPECT(!pat1.match("中X"));
    EXPECT(pat1.match("中aX"));

    PATDEF(pat2, "**/*?[!X]X")
    EXPECT(!pat2.match("/中X"));
    EXPECT(pat2.match("/中aX"));

    PATDEF(pat3, "*?[!X]X/**")
    EXPECT(!pat3.match("中X"));
    EXPECT(pat3.match("中aX"));
}

ZEST_CASE(invalid_utf8_input) {
    // A path byte that does not decode counts as one character that only
    // wildcards and negated classes can cover.
    PATDEF(pat1, "*.txt")
    EXPECT(pat1.match("\xFF\xFE.txt"));

    PATDEF(pat2, "??.txt")
    EXPECT(pat2.match("\xFF\xFE.txt"));

    PATDEF(pat3, "?.txt")
    EXPECT(!pat3.match("\xFF\xFE.txt"));
    EXPECT(pat3.match("\x80.txt"));

    PATDEF(pat4, "[!a].txt")
    EXPECT(pat4.match("\xFF.txt"));
    EXPECT(!pat4.match("a.txt"));

    // é (U+00E9) never equals the raw Latin-1 byte E9, in any construct.
    PATDEF(pat5, "é.txt")
    EXPECT(!pat5.match("\xE9.txt"));

    PATDEF(pat6, "[é].txt")
    EXPECT(!pat6.match("\xE9.txt"));

    PATDEF(pat7, "[!é].txt")
    EXPECT(pat7.match("\xE9.txt"));
}

ZEST_CASE(invalid_utf8_pattern) {
    auto expect_invalid = [](std::string_view pattern, std::uint32_t at) {
        auto res = kota::GlobPattern::create(pattern);
        EXPECT(!res.has_value());
        if(!res.has_value()) {
            EXPECT(res.error().kind == kota::GlobError::InvalidUtf8);
            EXPECT(res.error().begin == at);
        }
    };

    expect_invalid("\x80", 0);
    expect_invalid("caf\xC3", 3);
    expect_invalid("caf\xE9*", 3);
    expect_invalid("\xC0\x80", 0);
    expect_invalid("\xED\xA0\x80", 0);
    expect_invalid("\xE1\x80\x41", 0);
    expect_invalid("a[\xFF]", 2);
}

ZEST_CASE(escaped_slash) {
    auto expect_rejected = [](std::string_view pattern) {
        auto res = kota::GlobPattern::create(pattern);
        EXPECT(!res.has_value());
        if(!res.has_value()) {
            EXPECT(res.error().kind == kota::GlobError::InvalidEscape);
        }
    };

    expect_rejected(R"(\/)");
    expect_rejected(R"(a\/b)");
    expect_rejected(R"({a\/b,c})");
    expect_rejected(R"(**\/a)");

    // Escape parity: the first backslash escapes the second, the slash is
    // a real separator.
    PATDEF(pat1, R"(\\/)")
    EXPECT(pat1.match("\\/"));

    // Inside a class the slash is accepted but can never match.
    PATDEF(pat2, R"([\/])")
    EXPECT(!pat2.match("/"));
    EXPECT(!pat2.match("a"));
}

ZEST_CASE(empty_class) {
    EXPECT(!kota::GlobPattern::create("[]").has_value());

    PATDEF(pat1, "[]]")
    EXPECT(pat1.match("]"));

    EXPECT(!kota::GlobPattern::create("[!]").has_value());
    EXPECT(!kota::GlobPattern::create("[^]").has_value());

    PATDEF(pat3, "[!]]")
    EXPECT(!pat3.match("a]"));
    EXPECT(pat3.match("a"));
    EXPECT(!pat3.match("]"));

    PATDEF(pat4, "{[!]],[]]}")
    EXPECT(pat4.match("a"));
    EXPECT(pat4.match("]"));
    EXPECT(!pat4.match("/"));
}

ZEST_CASE(bracket_lookup) {
    // Disjoint ranges: membership must be found in the range whose upper
    // bound is the first to reach the probe.
    PATDEF(pat1, "[a-cx-z]")
    EXPECT(pat1.match("b"));
    EXPECT(pat1.match("y"));
    EXPECT(!pat1.match("d"));

    PATDEF(pat2, "[!a-cx-z]")
    EXPECT(!pat2.match("b"));
    EXPECT(pat2.match("d"));

    // Duplicates and overlaps coalesce without changing membership.
    PATDEF(pat3, "[aaa]")
    EXPECT(pat3.match("a"));
    EXPECT(!pat3.match("b"));

    PATDEF(pat4, "[a-mh-z]")
    EXPECT(pat4.match("h"));
    EXPECT(pat4.match("z"));
    EXPECT(!pat4.match("A"));
}

ZEST_CASE(unbounded_unicode_retries) {
    // Crossing the old retry threshold must not turn a true match into false.
    // The general class plan and direct suffix plan have the same semantics.
    auto rocket_input = [](size_t count) {
        std::string input;
        input.reserve(count * 4 + 1);
        for(size_t i = 0; i < count; i += 1) {
            input += "🚀";
        }
        input += "Z";
        return input;
    };

    PATDEF(pat1, "*[Z]")
    EXPECT(pat1.match(rocket_input(65535)));
    EXPECT(pat1.match(rocket_input(65536)));
    EXPECT(pat1.match(rocket_input(65537)));

    PATDEF(pat2, "*Z")
    EXPECT(pat2.match(rocket_input(65537)));
}

ZEST_CASE(vscode_star_semantics) {
    PATDEF(single, "*")
    EXPECT(single.match(""));
    EXPECT(!single.match("/"));
    EXPECT(!single.match("x/y"));

    for(std::string_view source: {"**.cpp", "a**b"}) {
        PATDEF(pattern, source)
        EXPECT(!pattern.match("a/x/b.cpp"));
        EXPECT(!pattern.match("a/x/b"));
    }
    PATDEF(embedded, "a**")
    EXPECT(embedded.match("a"));
    EXPECT(embedded.match("abc"));
    EXPECT(!embedded.match("a/b"));
    PATDEF(embedded_slash, "a**/")
    EXPECT(!embedded_slash.match("a"));
    EXPECT(embedded_slash.match("abc/"));
    EXPECT(!embedded_slash.match("a/b/"));
}

ZEST_CASE(compiled_string_predicates) {
    PATDEF(suffix, "**/*说明.cpp")
    EXPECT(suffix.match("文档/说明.cpp"));
    EXPECT(!suffix.match("文档/说明.cpp/"));
    PATDEF(segment, "foo**.cpp")
    EXPECT(segment.match("foo.cpp"));
    EXPECT(!segment.match("foo/x.cpp"));
    PATDEF(path, "**/中/文")
    EXPECT(path.match("x/中/文"));
    EXPECT(!path.match("x中/文"));
    PATDEF(tree, "**/中/文/**")
    EXPECT(tree.match("中/文"));
    EXPECT(tree.match("x/中/文/y"));
    EXPECT(tree.match("x中/文z/中/文/y"));
    EXPECT(!tree.match("x中/文/y"));
    EXPECT(!tree.match("中/文字/y"));
    PATDEF(escaped, R"(**/\*.cpp)")
    EXPECT(escaped.match("x/*.cpp"));
    EXPECT(!escaped.match("x/a.cpp"));
    PATDEF(leading_slash, "/**/*.cpp")
    EXPECT(leading_slash.match("/x/a.cpp"));
    EXPECT(!leading_slash.match("x/a.cpp"));
    PATDEF(basename_class, "src/**/test[0-9].cpp")
    EXPECT(basename_class.match("src/a/b/test1.cpp"));
    EXPECT(!basename_class.match("src/test1.cpp/child"));
    EXPECT(!basename_class.match("src/a/test10.cpp"));
    EXPECT(!basename_class.match("other/test1.cpp"));
}

ZEST_CASE(packed_suffix_boundaries) {
    std::vector<std::string> literals = {"", "说明.cpp", "🚀", std::string("\0z", 2)};
    for(size_t length: {1, 7, 8, 9, 15, 16, 17, 24}) {
        literals.emplace_back(length, 'x');
    }
    for(const auto& literal: literals) {
        PATDEF(recursive, "**/*" + literal)
        PATDEF(segment, "*" + literal)
        auto copied = recursive;
        for(size_t padding = 0; padding < 25; ++padding) {
            for(const auto& text: {std::string(padding, 'q'),
                                   std::string(padding, 'q') + literal,
                                   "/" + std::string(padding, 'q') + literal,
                                   std::string(padding, 'q') + literal + 'q'}) {
                // An exact allocation gives ASan redzones immediately before
                // and after the view, including inputs shorter than a word.
                auto storage = std::make_unique<char[]>(text.size());
                std::copy(text.begin(), text.end(), storage.get());
                std::string_view view(storage.get(), text.size());
                EXPECT(recursive.match(view) == view.ends_with(literal));
                EXPECT(copied.match(view) == view.ends_with(literal));
                EXPECT(segment.match(view) == (view.ends_with(literal) && !view.contains('/')));
            }
        }
    }
}

ZEST_CASE(extension_set_boundaries) {
    for(size_t count: {15, 16, 17, 50, 100}) {
        std::vector<std::string> extensions = {".", ".中", ".🚀", ".abcdefg"};
        while(extensions.size() < count) {
            extensions.push_back(std::format(".e{}", extensions.size()));
        }
        auto check = [&] {
            std::string source = "**/*.{";
            for(size_t i = 0; i < extensions.size(); ++i) {
                if(i != 0)
                    source += ',';
                source += extensions[i].substr(1);
            }
            source += '}';
            PATDEF(pattern, source)
            auto moved = std::move(pattern);
            for(const auto& extension: extensions) {
                for(const auto& path: {extension,
                                       "file" + extension,
                                       "路径/file" + extension,
                                       "dir" + extension + "/file",
                                       "file" + extension + 'q'}) {
                    bool expected = std::ranges::any_of(extensions, [&](const auto& suffix) {
                        return path.ends_with(suffix);
                    });
                    EXPECT(moved.match(path) == expected);
                }
            }
            EXPECT(!moved.match("no_extension"));
        };
        check();
        // These must select the general suffix set rather than treating the
        // last dot as the only possible extension boundary.
        extensions.back() = ".d.ts";
        check();
        extensions.back() = ".abcdefgh";
        check();
    }
}

ZEST_CASE(redundant_star_backtracking) {
    // The first candidate matches through 'b' but fails at /x. Retrying
    // every distribution among the single stars used to exhaust the
    // budget before the globstar could reach the second candidate.
    PATDEF(pat1, "**/*a*a*a*ab/x")
    EXPECT(pat1.match(std::string(32, 'a') + "b/y/aaaab/x"));
    EXPECT(!pat1.match(std::string(32, 'a') + "b/y/aaaab/y"));

    // Dropping a star must preserve the bracket index of the newer state,
    // and Unicode characters must still be consumed as whole atoms.
    PATDEF(pat2, "**/*[中]*?*[中]*[中]b/x")
    std::string prefix;
    for(size_t i = 0; i < 32; i += 1) {
        prefix += "中";
    }
    EXPECT(pat2.match(prefix + "b/y/中中中中b/x"));
    EXPECT(!pat2.match(prefix + "b/y/中中中b/x"));

    PATDEF(pat3, "**/*a*a/**/b/x")
    EXPECT(pat3.match("aaaa/other/b/x"));
    EXPECT(!pat3.match("aaaa/other/b/y"));
}

ZEST_CASE(globstar_boundary_retries) {
    // Directory length must not spend one retry per character when the
    // next pattern segment can only start after a separator.
    auto long_directory = std::string(70000, 'a');
    PATDEF(pat1, "**/目标.cpp")
    EXPECT(pat1.match(long_directory + "/目标.cpp"));
    EXPECT(!pat1.match(long_directory + "目标.cpp"));

    PATDEF(pat2, "源**/?.cpp")
    EXPECT(pat2.match("源" + long_directory + "/中.cpp"));
    EXPECT(!pat2.match("源中.cpp"));
    EXPECT(!pat2.match("源" + long_directory + "/中文.cpp"));

    PATDEF(pat3, "**/[!a].cpp")
    EXPECT(pat3.match(std::string("\xFF\x80/\xFE.cpp")));
    EXPECT(!pat3.match(std::string("\xFF\x80\xFE.cpp")));

    PATDEF(pat4, "**/a/")
    EXPECT(pat4.match("a/a/"));
    EXPECT(!pat4.match("a/a/x"));
}

ZEST_CASE(completed_segment_retries) {
    // A final star fixes the segment endpoint. Redistributing earlier stars
    // in already matched segments used to exhaust the budget before **
    // could reach the valid suffix.
    PATDEF(pat1, "**/*a*/*a*/*a*/c")
    auto segment = std::string(48, 'a') + '/';
    EXPECT(pat1.match(segment + segment + segment + "y/a/a/a/c"));
    EXPECT(!pat1.match(segment + segment + segment + "y/a/a/a/d"));

    PATDEF(pat2, "**/*a*/b")
    EXPECT(pat2.match(std::string(70000, 'a') + "/c/a/b"));
    EXPECT(!pat2.match(std::string(70000, 'a') + "/c/a/c"));

    PATDEF(pat3, "**/[中]*[文]*/[🚀]*/目标")
    EXPECT(pat3.match("中文/🚀/错/中文/🚀/目标"));
    EXPECT(!pat3.match("中文/🚀/错/中文/x/目标"));
}

ZEST_CASE(latest_globstar_checkpoint) {
    PATDEF(pat1, "**/a/**/b/**/c")
    EXPECT(pat1.match("a/x/a/b/y/b/c"));
    EXPECT(!pat1.match("a/x/a/b/y/b/d"));

    PATDEF(pat2, "**/[中]*[文]/**/[🚀]/目标")
    EXPECT(pat2.match("中文/中x文/🚀/错/🚀/目标"));
    EXPECT(!pat2.match("中文/中x文/🚀/错/x/目标"));

    // More checkpoints than the old stack's inline capacity, including a
    // deep copy of the compiled pattern. The new matcher has no stack.
    std::string source;
    std::string input;
    for(size_t i = 0; i < 24; i += 1) {
        source += "**/a/";
        input += "x/a/";
    }
    source += "目标";
    PATDEF(pat3, source)
    auto copy = pat3;
    EXPECT(copy.match(input + "目标"));
    EXPECT(!copy.match(input + "错"));
}

ZEST_CASE(globstar_single_star_handover) {
    PATDEF(pat1, "**/a*b")
    EXPECT(pat1.match(std::string(1000, 'a') + "/ab"));
    EXPECT(!pat1.match(std::string(1000, 'a') + "/ac"));

    PATDEF(pat2, "**/中*?b")
    std::string prefix;
    for(size_t i = 0; i < 1000; i += 1) {
        prefix += "中";
    }
    EXPECT(pat2.match(prefix + "/中🚀b"));
    EXPECT(!pat2.match(prefix + "/中b"));

    // A pattern separator between the checkpoints prevents skipping: the
    // second 'a' must be reconsidered as the start of the whole suffix.
    PATDEF(pat3, "**/a/*b/c")
    EXPECT(pat3.match("a/a/b/c"));

    PATDEF(pat4, "**/[a/]*b")
    EXPECT(pat4.match("aaaa/ac/ab"));
    EXPECT(!pat4.match("aaaa/ac/ac"));
}

ZEST_CASE(separator_token_boundaries) {
    // Slashes inside a character class must never be mistaken for pattern
    // separators when deriving boundaries directly from the pattern bytes.
    PATDEF(pat1, "**/[a/]b/目标")
    EXPECT(pat1.match("x/ab/目标"));
    EXPECT(!pat1.match("x//b/目标"));

    PATDEF(pat2, R"(**/[\/]*/目标)")
    EXPECT(!pat2.match("x//目标"));

    PATDEF(pat3, R"(**/\\/目标)")
    EXPECT(pat3.match("x/\\/目标"));
    EXPECT(!pat3.match("x/目标"));

    PATDEF(pat4, "**/{中,文}/")
    EXPECT(pat4.match("x/中/文/"));
    EXPECT(!pat4.match("x/中/文"));
}

ZEST_CASE(generated_segment_reference) {
    // Independent dynamic programming over generated tokens/atoms: no
    // production parser, decoder, prefix extraction or backtracking is
    // used by the oracle. This covers normalized paths, with whole-segment
    // globstars; separate examples pin the extended glob syntax.
    struct Token {
        std::string_view text;
        unsigned mask;
        bool star = false;
    };

    constexpr std::array<std::string_view, 5> atoms = {"a", "b", "中", "🚀", "\xFF"};
    constexpr std::array<Token, 9> tokens = {
        {{"a", 1},
         {"b", 2},
         {"中", 4},
         {"🚀", 8},
         {"?", 31},
         {"[ab]", 3},
         {"[!中]", 27},
         {R"(\中)", 4},
         {"*", 31, true}}
    };

    struct Segment {
        bool globstar;
        std::vector<size_t> members;
    };

    auto segment_match = [&](const Segment& segment, const std::vector<size_t>& word) {
        std::vector<bool> previous(word.size() + 1);
        previous[0] = true;
        for(auto member: segment.members) {
            const auto& token = tokens[member];
            std::vector<bool> next(word.size() + 1);
            next[0] = token.star && previous[0];
            for(size_t j = 1; j <= word.size(); j += 1) {
                next[j] = token.star ? previous[j] || next[j - 1]
                                     : previous[j - 1] && (token.mask & (1u << word[j - 1]));
            }
            previous = std::move(next);
        }
        return bool(previous.back());
    };

    std::mt19937 random(205);
    for(size_t trial = 0; trial < 4000; trial += 1) {
        std::vector<Segment> segments;
        std::string source;
        size_t segment_count = 1 + random() % 5;
        for(size_t i = 0; i < segment_count; i += 1) {
            if(i != 0) {
                source += '/';
            }
            auto& segment = segments.emplace_back(random() % 4 == 0, std::vector<size_t>{});
            if(segment.globstar) {
                source += "**";
                continue;
            }
            size_t count = 1 + random() % 5;
            for(size_t j = 0; j < count; j += 1) {
                auto member = random() % tokens.size();
                // Adjacent single stars would spell a different operator.
                if(!segment.members.empty() && tokens[segment.members.back()].star &&
                   tokens[member].star) {
                    member = 0;
                }
                segment.members.push_back(member);
                source += tokens[member].text;
            }
        }
        PATDEF(pattern, source)
        for(size_t sample = 0; sample < 16; sample += 1) {
            std::vector<std::vector<size_t>> words(1 + random() % 5);
            std::string input;
            for(auto& word: words) {
                if(!input.empty()) {
                    input += '/';
                }
                size_t count = 1 + random() % 6;
                for(size_t j = 0; j < count; j += 1) {
                    auto atom = random() % atoms.size();
                    word.push_back(atom);
                    input += atoms[atom];
                }
            }
            std::vector<bool> previous(words.size() + 1);
            previous[0] = true;
            for(const auto& segment: segments) {
                std::vector<bool> next(words.size() + 1);
                next[0] = segment.globstar && previous[0];
                for(size_t j = 1; j <= words.size(); j += 1) {
                    next[j] = segment.globstar
                                  ? previous[j] || next[j - 1]
                                  : previous[j - 1] && segment_match(segment, words[j - 1]);
                }
                previous = std::move(next);
            }
            bool expected = source == "**" || previous.back();
            if(auto actual = pattern.match(input); actual != expected) {
                EXPECT(std::format("{} / {} -> {}", source, input, actual) ==
                       std::format("{} / {} -> {}", source, input, expected));
                return;
            }
        }
    }
}

ZEST_CASE(compiled_segments_without_retry_cutoff) {
    for(size_t width: {20, 80}) {
        for(size_t count: {80, 160}) {
            std::string source = "**/";
            for(size_t i = 0; i < 60; ++i) {
                source += "*a/";
            }
            source += "Z";
            std::string input;
            for(size_t i = 0; i < count; ++i) {
                input += std::string(width, 'b') + "a/";
            }
            input += 'Z';
            PATDEF(pattern, source)
            EXPECT(pattern.match(input));
            input.back() = 'Y';
            EXPECT(!pattern.match(input));
        }
    }
    // The segment matcher must not inherit the old per-byte retry cap either.
    PATDEF(long_segment, "{*a?Z,never}")
    std::string input(70000, 'b');
    EXPECT(long_segment.match(input + "a中Z"));
    EXPECT(!long_segment.match(input + "a中Y"));
}

ZEST_CASE(compiled_segment_literals_and_copy) {
    PATDEF(pattern, R"(src/*/test_\[中\]*.cpp)")
    EXPECT(pattern.match("src/目录/test_[中]文.cpp"));
    EXPECT(pattern.match("src//test_[中].cpp"));
    EXPECT(!pattern.match("src/目录/sub/test_[中].cpp"));
    EXPECT(!pattern.match("src/目录/test_中.cpp"));
    auto copy = pattern;
    auto moved = std::move(copy);
    EXPECT(moved.match("src/x/test_[中].cpp"));
    pattern = *GlobPattern::create("other");
    EXPECT(moved.match("src/x/test_[中].cpp"));
    PATDEF(classes, "src/*/[中a]?*.cpp")
    EXPECT(classes.match(std::string("src/x/a") + '\xff' + ".cpp"));
    EXPECT(!classes.match("src/x/a.cpp"));
}

ZEST_CASE(escaped_literal_prefix_plans) {
    PATDEF(tree, R"(/work/项目\[demo\]/src/**/*.cpp)")
    EXPECT(tree.match("/work/项目[demo]/src/test.cpp"));
    EXPECT(tree.match("/work/项目[demo]/src/目录/test.cpp"));
    EXPECT(!tree.match("/work/项目[demo]/srcx/test.cpp"));
    EXPECT(!tree.match("/work/项目d/src/test.cpp"));
    PATDEF(exact, R"(\中\文\*\?\[\{\\)")
    EXPECT(exact.match("中文*?[{\\"));
    EXPECT(!exact.match("中文*?[{\\x"));
    EXPECT(!GlobPattern::create(R"(a\[//b)").has_value());
    EXPECT(!GlobPattern::create(R"(a\[\/b)").has_value());
    EXPECT(!GlobPattern::create(R"(a\[b\)").has_value());
    auto copy = tree;
    auto moved = std::move(copy);
    EXPECT(moved.match("/work/项目[demo]/src/test.cpp"));
}

ZEST_CASE(affix_and_brace_tree_plans) {
    PATDEF(affix, "**/foo*foo")
    EXPECT(!affix.match("foo"));
    EXPECT(!affix.match("foo/foo"));
    EXPECT(affix.match("foofoo"));
    EXPECT(affix.match("目录/foo中文foo"));
    EXPECT(!affix.match("目录/foofoo/"));
    PATDEF(prefix, "**/test_*")
    EXPECT(prefix.match("目录/test_"));
    EXPECT(!prefix.match("目录/test_/child"));
    PATDEF(arms, "{test_*.cpp,foo**foo}")
    EXPECT(arms.match("test_.cpp"));
    EXPECT(arms.match("foofoo"));
    EXPECT(!arms.match("foo/foo"));
    PATDEF(tree, "{src,include}/**")
    for(std::string_view input: {"src", "src/", "src/中文.cpp", "include/a/b"}) {
        EXPECT(tree.match(input));
    }
    for(std::string_view input: {"", "/src", "srcx/a", "x/include/a"}) {
        EXPECT(!tree.match(input));
    }
    PATDEF(root, "{,src}/**")
    EXPECT(root.match(""));
    EXPECT(root.match("/foo"));
    EXPECT(!root.match("foo"));
    std::string source("**/中*\0文", 11);
    PATDEF(binary, source)
    EXPECT(binary.match(std::string("中\0文", 7)));
    EXPECT(!binary.match("中文"));
}

// The wildcard and range cases below are ported from rust-lang/glob's
// test suite (MIT/Apache-2.0).
ZEST_CASE(ported_wildcards) {
    PATDEF(pat1, "a*b")
    EXPECT(pat1.match("a_b"));

    PATDEF(pat2, "a*b*c")
    EXPECT(pat2.match("abc"));
    EXPECT(!pat2.match("abcd"));
    EXPECT(pat2.match("a_b_c"));
    EXPECT(pat2.match("a___b___c"));

    PATDEF(pat3, "abc*abc*abc")
    EXPECT(pat3.match("abcabcabcabcabcabcabc"));
    EXPECT(!pat3.match("abcabcabcabcabcabcabca"));

    PATDEF(pat4, "a*a*a*a*a*a*a*a*a")
    EXPECT(pat4.match("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));

    PATDEF(pat5, "a*b[xyz]c*d")
    EXPECT(pat5.match("abxcdbxcddd"));
}

ZEST_CASE(ported_ranges) {
    PATDEF(pat1, "a[0-9]b")
    for(char digit = '0'; digit <= '9'; digit += 1) {
        EXPECT(pat1.match(std::string("a") + digit + "b"));
    }
    EXPECT(!pat1.match("a_b"));

    PATDEF(pat2, "a[!0-9]b")
    for(char digit = '0'; digit <= '9'; digit += 1) {
        EXPECT(!pat2.match(std::string("a") + digit + "b"));
    }
    EXPECT(pat2.match("a_b"));

    for(std::string_view p: {"[a-z123]", "[1a-z23]", "[123a-z]"}) {
        PATDEF(pat, p)
        for(char c = 'a'; c <= 'z'; c += 1) {
            EXPECT(pat.match(std::string_view(&c, 1)));
        }
        EXPECT(pat.match("1"));
        EXPECT(pat.match("2"));
        EXPECT(pat.match("3"));
    }

    for(std::string_view p: {"[abc-]", "[-abc]", "[a-c-]"}) {
        PATDEF(pat, p)
        EXPECT(pat.match("a"));
        EXPECT(pat.match("b"));
        EXPECT(pat.match("c"));
        EXPECT(pat.match("-"));
        EXPECT(!pat.match("d"));
    }

    PATDEF(pat3, "[-]")
    EXPECT(pat3.match("-"));

    PATDEF(pat4, "[!-]")
    EXPECT(!pat4.match("-"));
}

};  // namespace kota

}  // namespace

}  // namespace kota
