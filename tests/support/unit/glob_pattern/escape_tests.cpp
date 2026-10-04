#include <string>
#include <string_view>

#include "support/harness/glob.h"
#include "kota/zest/zest.h"
#include "kota/support/glob_pattern.h"

namespace kota {

namespace {

// GlobPattern::escape(): a literal as the pattern that matches it alone.

ZEST_SUITE(support_glob_pattern_escape) {

ZEST_CASE(escape_puts_a_backslash_before_each_special_byte) {
    EXPECT(GlobPattern::escape(R"(a*b?c[d]e{f,g}h\i)") == R"(a\*b\?c\[d\]e\{f\,g\}h\\i)");
    EXPECT(GlobPattern::escape("plain/path name.txt") == "plain/path name.txt");
    EXPECT(GlobPattern::escape("").empty());
}

ZEST_CASE(escaped_ascii_byte_matches_itself_alone) {
    constexpr std::string_view special = R"(\?*[]{},)";
    for(int byte = 0x01; byte < 0x80; ++byte) {
        auto literal = std::string(1, static_cast<char>(byte));
        ZEST_CONTEXT("byte {:#04x}", byte);
        auto escaped = GlobPattern::escape(literal);
        EXPECT(escaped == (special.contains(literal[0]) ? "\\" + literal : literal));
        test::expect_glob(escaped, {literal}, {byte == 'a' ? "b" : "a"});
    }
}

ZEST_CASE(escaped_literal_matches_itself_alone) {
    test::expect_glob(GlobPattern::escape("a*b"), {"a*b"}, {"axb", "ab"});
    test::expect_glob(GlobPattern::escape("x?y"), {"x?y"}, {"xzy"});
    test::expect_glob(GlobPattern::escape("[ab]"), {"[ab]"}, {"a", "b"});
    test::expect_glob(GlobPattern::escape("{a,b}"), {"{a,b}"}, {"a", "b"});
    test::expect_glob(GlobPattern::escape(R"(c\d)"), {R"(c\d)"}, {"cd"});
    test::expect_glob(GlobPattern::escape("x]y}z,w"), {"x]y}z,w"});
    test::expect_glob(GlobPattern::escape("src/**/main.cpp"),
                      {"src/**/main.cpp"},
                      {"src/main.cpp", "src/a/main.cpp"});
    test::expect_glob(GlobPattern::escape("项目/文件*.cpp"),
                      {"项目/文件*.cpp"},
                      {"项目/文件a.cpp"});
}

// Its `,` and `}` escaped, a literal is one term of a brace expression.
ZEST_CASE(escaped_literal_stands_as_a_brace_term) {
    auto pattern = "{" + GlobPattern::escape("a,b}") + ",c}";
    test::expect_glob(pattern, {"a,b}", "c"}, {"a", "b}", "a,b"});

    auto specials = std::string(R"(\?*[]{},)");
    test::expect_glob("{" + GlobPattern::escape(specials) + ",c}", {specials, "c"}, {"", "?"});
}

// `/` cannot be escaped, so a literal with `//` has no pattern.
ZEST_CASE(escaped_literal_with_a_double_slash_fails) {
    test::expect_glob_error(GlobPattern::escape("a//b*"), GlobError::MultipleSlash, 1, 3);
}

ZEST_CASE(escaped_literal_that_is_not_utf8_fails) {
    test::expect_glob_error(GlobPattern::escape("a*\xFF"), GlobError::InvalidUtf8, 3, 4);
}

};  // ZEST_SUITE(support_glob_pattern_escape)

}  // namespace

}  // namespace kota
