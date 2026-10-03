#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "support/harness/glob.h"
#include "kota/zest/zest.h"
#include "kota/support/glob_pattern.h"

namespace kota {

namespace {

// GlobPattern::escape(): a literal as the pattern that matches it and nothing else, and
// unescape(), back.

ZEST_SUITE(support_glob_pattern_escape) {

ZEST_CASE(each_metacharacter_takes_a_backslash) {
    EXPECT(GlobPattern::escape(R"(a*b?c[d]e{f,g}h\i)") == R"(a\*b\?c\[d]e\{f\,g\}h\\i)");
    EXPECT(GlobPattern::escape("plain/name.txt") == "plain/name.txt");
    EXPECT(GlobPattern::escape("") == "");
}

ZEST_CASE(unescape_inverts_escape) {
    constexpr std::string_view literals[] = {
        R"(a*b?c[d]e{f,g}h\i)",
        R"(\\)",
        "**/{,}[!]-^",
        "项目[demo]/src",
        "plain/name.txt",
        "",
    };
    for(auto literal: literals) {
        ZEST_CONTEXT("literal `{}`", literal);
        EXPECT(GlobPattern::unescape(GlobPattern::escape(literal)) == literal);
    }
}

ZEST_CASE(unescape_drops_each_escaping_backslash) {
    // An escaped character that is no wildcard stands for itself, as create() reads it.
    EXPECT(GlobPattern::unescape(R"(a\*b\\c\d)") == R"(a*b\cd)");
    // Wildcards are copied as they are.
    EXPECT(GlobPattern::unescape(R"(src/*.{h,cpp})") == R"(src/*.{h,cpp})");
}

ZEST_CASE(unescape_keeps_a_lone_backslash_at_the_end) {
    // It escapes nothing; create() rejects such a pattern.
    EXPECT(GlobPattern::unescape(R"(a\)") == R"(a\)");
    EXPECT(GlobPattern::unescape(R"(a\\\)") == R"(a\\)");
}

ZEST_CASE(escaped_literals_match_only_themselves) {
    // Each literal, then a path its unescaped text would match too.
    constexpr std::pair<std::string_view, std::string_view> cases[] = {
        {"a*b",            "aXb"        },
        {"x?y",            "xzy"        },
        {"[abc]",          "a"          },
        {"{a,b}",          "a"          },
        {"{,a}",           "a"          },
        {"**",             "deep/path"  },
        {"src/**/*.cpp",   "src/a/b.cpp"},
        {"项目[demo]/*.h", "项目d/x.h"  },
        {R"(back\slash)",  "backslash"  },
        {"![!x]-^",        "!y-^"       },
    };
    for(auto [literal, other]: cases) {
        const auto escaped = GlobPattern::escape(literal);
        ZEST_CONTEXT("literal `{}`, escaped `{}`", literal, escaped);
        auto compiled = GlobPattern::create(escaped);
        ASSERT(compiled.has_value());
        EXPECT(compiled->match(literal));
        EXPECT(!compiled->match(other));
    }
}

ZEST_CASE(escaped_literals_stand_as_brace_terms) {
    const auto pattern =
        std::format("out/{{{},{}}}.log", GlobPattern::escape("a,b"), GlobPattern::escape("c}d"));
    test::expect_glob(pattern, {"out/a,b.log", "out/c}d.log"}, {"out/a.log", "out/b.log"});
}

ZEST_CASE(escaped_literals_extend_a_pattern) {
    const auto pattern = "**/" + GlobPattern::escape("[gen]") + "/*.h";
    test::expect_glob(pattern, {"x/[gen]/a.h", "[gen]/b.h"}, {"x/g/a.h"});
}

ZEST_CASE(slashes_stay_separators) {
    EXPECT(GlobPattern::escape("dir/*.h") == R"(dir/\*.h)");
    test::expect_glob(GlobPattern::escape("dir/*.h"), {"dir/*.h"}, {"dir/a.h", "dir"});
}

ZEST_CASE(literal_with_a_double_slash_fails) {
    // No pattern matches `//`: the escape keeps the slashes, which create() rejects.
    test::expect_glob_error(GlobPattern::escape("a//b*"), GlobError::MultipleSlash, 1, 3);
}

};  // ZEST_SUITE(support_glob_pattern_escape)

}  // namespace

}  // namespace kota
