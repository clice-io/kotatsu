#include <source_location>
#include <string>
#include <string_view>

#include "support/harness/glob.h"
#include "kota/zest/zest.h"
#include "kota/support/glob_pattern.h"

namespace kota {

namespace {

// GlobPattern::split_root(): the directory a pattern starts with, before any
// wildcard, with its escapes resolved, and the rest as written.

/// Checks that `pattern` splits into `directory` and `rest`, the rest a slice
/// of the pattern.
void expect_split(std::string_view pattern,
                  std::string_view directory,
                  std::string_view rest,
                  std::source_location location = std::source_location::current()) {
    ZEST_CONTEXT("glob `{}`, checked at line {}", pattern, location.line());
    auto root = GlobPattern::split_root(pattern);
    EXPECT(root.directory == directory);
    EXPECT(root.rest == rest);
    // The pointers, not the text they point to.
    EXPECT((root.rest.data() == pattern.data() + pattern.size() - rest.size()));
}

ZEST_SUITE(support_glob_pattern_root) {

ZEST_CASE(split_root_cuts_at_the_last_slash_before_a_wildcard) {
    expect_split("src/a{b,c}/*.cpp", "src/", "a{b,c}/*.cpp");
    expect_split("src/lib/**/*.h", "src/lib/", "**/*.h");
    expect_split("/usr/include/*.h", "/usr/include/", "*.h");
    expect_split("a/b?/c", "a/", "b?/c");
    expect_split("a/b/[ch]", "a/b/", "[ch]");
    expect_split("项目/src/*.cpp", "项目/src/", "*.cpp");
    expect_split("/*", "/", "*");
}

ZEST_CASE(split_root_of_a_first_segment_wildcard_has_no_directory) {
    expect_split("*.cpp", "", "*.cpp");
    expect_split("**/x.h", "", "**/x.h");
    expect_split("a{b,c}/d", "", "a{b,c}/d");
    expect_split("[ab]/c/d", "", "[ab]/c/d");
}

ZEST_CASE(split_root_without_wildcards_cuts_at_the_last_slash) {
    expect_split("src/main.cpp", "src/", "main.cpp");
    expect_split("main.cpp", "", "main.cpp");
    expect_split("src/dir/", "src/dir/", "");
    expect_split("", "", "");
}

ZEST_CASE(split_root_resolves_the_directory_escapes) {
    expect_split(R"(a\*b/c\[1\]/*.h)", "a*b/c[1]/", "*.h");
    expect_split(R"(x\{y\}/z)", "x{y}/", "z");
    // An escaped backslash leaves the slash after it a separator.
    expect_split(R"(x\\/y*)", R"(x\/)", "y*");
    // An escaped wildcard does not cut.
    expect_split(R"(a/b\*c/d*)", "a/b*c/", "d*");
}

// The escapes create() rejects end the root, as a wildcard does.
ZEST_CASE(split_root_ends_at_an_escape_create_rejects) {
    expect_split(R"(a\/b/c*)", "", R"(a\/b/c*)");
    expect_split(R"(a/b\)", "a/", R"(b\)");
    expect_split(R"(\)", "", R"(\)");
}

// The use split_root() is for: the directory resolved on its own, then escaped
// back in front of the rest, gives a pattern that matches as the first did.
ZEST_CASE(split_root_rebuilds_a_pattern_matching_alike) {
    auto pattern = std::string_view(R"(src/a\[1\]/**/*.cpp)");
    auto root = GlobPattern::split_root(pattern);
    EXPECT(root.directory == "src/a[1]/");
    auto rebuilt = GlobPattern::escape(root.directory) + std::string(root.rest);
    for(std::string_view each: {pattern, std::string_view(rebuilt)}) {
        test::expect_glob(each,
                          {"src/a[1]/x.cpp", "src/a[1]/b/c/y.cpp"},
                          {"src/a1/x.cpp", "src/a[1]/x.h"});
    }
}

};  // ZEST_SUITE(support_glob_pattern_root)

}  // namespace

}  // namespace kota
