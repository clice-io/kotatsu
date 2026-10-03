#include <source_location>
#include <string_view>

#include "kota/zest/zest.h"
#include "kota/support/glob_pattern.h"

namespace kota {

namespace {

// GlobPattern::split_literal_root(): the directory a pattern starts with, before any wildcard,
// and the rest, both as written; unescape() turns the first into the directory's path.

/// Checks that `pattern` splits into `root` and `rest`, two slices of it.
void expect_split(std::string_view pattern,
                  std::string_view root,
                  std::string_view rest,
                  std::source_location location = std::source_location::current()) {
    ZEST_CONTEXT("glob `{}`, checked at line {}", pattern, location.line());
    const auto [split_root, split_rest] = GlobPattern::split_literal_root(pattern);
    EXPECT(split_root == root);
    EXPECT(split_rest == rest);
    // The pointers, not the text they point to.
    EXPECT((split_root.data() == pattern.data()));
    EXPECT((split_rest.data() == pattern.data() + split_root.size()));
}

ZEST_SUITE(support_glob_pattern_literal_root) {

ZEST_CASE(root_ends_at_the_last_slash_before_a_wildcard) {
    expect_split("src/a{b,c}/*.cpp", "src/", "a{b,c}/*.cpp");
    expect_split("src/lib/**/*.h", "src/lib/", "**/*.h");
    expect_split("/usr/include/*.h", "/usr/include/", "*.h");
    expect_split("a/b?/c", "a/", "b?/c");
    expect_split("a/b/[ch]", "a/b/", "[ch]");
    expect_split("项目/src/*.cpp", "项目/src/", "*.cpp");
}

ZEST_CASE(wildcard_in_the_first_segment_leaves_no_root) {
    expect_split("*.cpp", "", "*.cpp");
    expect_split("**/x.h", "", "**/x.h");
    expect_split("a{b,c}/d", "", "a{b,c}/d");
    expect_split("[ab]/c/d", "", "[ab]/c/d");
    expect_split("/*", "/", "*");
}

ZEST_CASE(pattern_without_wildcards_splits_at_its_last_slash) {
    expect_split("src/main.cpp", "src/", "main.cpp");
    expect_split("main.cpp", "", "main.cpp");
    expect_split("src/dir/", "src/dir/", "");
    expect_split("", "", "");
}

ZEST_CASE(escapes_stay_in_the_root) {
    expect_split(R"(a\*b/c\[1\]/*.h)", R"(a\*b/c\[1\]/)", "*.h");
    expect_split(R"(x\{y\}/z)", R"(x\{y\}/)", "z");
    // An escaped backslash leaves the slash after it a separator.
    expect_split(R"(x\\/y*)", R"(x\\/)", "y*");
    // A trailing backslash ends the scan.
    expect_split(R"(a/b\)", "a/", R"(b\)");
}

ZEST_CASE(unescaped_root_is_the_directory) {
    const auto [root, rest] =
        GlobPattern::split_literal_root(R"(work/\[demo\]/{src,include}/**/*.h)");
    EXPECT(GlobPattern::unescape(root) == "work/[demo]/");
    EXPECT(rest == "{src,include}/**/*.h");
}

};  // ZEST_SUITE(support_glob_pattern_literal_root)

}  // namespace

}  // namespace kota
