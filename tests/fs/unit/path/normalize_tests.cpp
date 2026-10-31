#include <string>
#include <string_view>

#include "kotatsu/zest/zest.h"
#include "kotatsu/support/small_string.h"
#include "kotatsu/fs/path.h"

namespace kotatsu::fs::path {

namespace {

constexpr auto posix = Style::Posix;
constexpr auto windows = Style::Windows;

/// `p` normalized, `..` folded.
std::string folded(std::string_view p, Style style) {
    std::string buffer(p);
    normalize(buffer, DotDot::Fold, style);
    return buffer;
}

/// `p` normalized, `..` kept.
std::string kept(std::string_view p, Style style) {
    std::string buffer(p);
    normalize(buffer, DotDot::Keep, style);
    return buffer;
}

ZEST_SUITE(fs_path_normalize) {

ZEST_CASE(normalize_rewrites_the_buffer_in_place) {
    small_string<16> p("a/./b/../c//");
    normalize(p, DotDot::Fold, posix);
    ZEXPECT(std::string_view(p) == "a/c");
    p = "./";
    normalize(p, DotDot::Fold, posix);
    ZEXPECT(std::string_view(p) == ".");
}

ZEST_CASE(dots_and_repeated_separators_go) {
    ZEXPECT(folded("a/./b//c/", posix) == "a/b/c");
    ZEXPECT(folded("./a", posix) == "a");
    ZEXPECT(folded("//a", posix) == "/a");
    ZEXPECT(folded("/", posix) == "/");
    ZEXPECT(folded(".", posix) == ".");
    ZEXPECT(folded("./", posix) == ".");
    ZEXPECT(folded("", posix) == "");
}

ZEST_CASE(dot_dot_folds_with_the_name_before_it) {
    ZEXPECT(folded("a/b/../c", posix) == "a/c");
    ZEXPECT(folded("a/..", posix) == ".");
    ZEXPECT(folded("a/../..", posix) == "..");
    ZEXPECT(folded("../a/../b", posix) == "../b");
    ZEXPECT(folded("../..", posix) == "../..");
}

ZEST_CASE(dot_dot_never_climbs_past_a_root) {
    ZEXPECT(folded("/../a", posix) == "/a");
    ZEXPECT(folded("/..", posix) == "/");
    ZEXPECT(folded("C:\\..\\a", windows) == "C:\\a");
    ZEXPECT(folded(R"(C:\..)", windows) == R"(C:\)");
    ZEXPECT(folded(R"(\\server\share\..\..\a)", windows) == R"(\\server\share\a)");
    ZEXPECT(folded(R"(\\?\C:\..\a)", windows) == R"(\\?\C:\a)");
    // A bare drive's directory is unknown, so `..` may climb past it.
    ZEXPECT(folded("C:..\\a", windows) == "C:..\\a");
    ZEXPECT(folded("C:a\\..\\..", windows) == "C:..");
}

ZEST_CASE(a_root_alone_stays_as_written) {
    ZEXPECT(folded("C:", windows) == "C:");
    ZEXPECT(folded(R"(C:\)", windows) == R"(C:\)");
    ZEXPECT(folded(R"(\\server\share)", windows) == R"(\\server\share)");
}

ZEST_CASE(dot_dot_stays_when_asked) {
    ZEXPECT(kept("a/./b/../c/", posix) == "a/b/../c");
    ZEXPECT(kept("/../a", posix) == "/../a");
}

ZEST_CASE(windows_paths_keep_their_first_separator) {
    ZEXPECT(folded("C:\\a/b\\c", windows) == "C:\\a\\b\\c");
    ZEXPECT(folded("C:/a\\b", windows) == "C:/a/b");
    ZEXPECT(folded("//server/share\\a", windows) == "//server/share/a");
    ZEXPECT(folded("a\\b", posix) == "a\\b");
    // Only `\` separates past `\\?\`.
    ZEXPECT(folded(R"(\\?\C:\a/b\.\c)", windows) == R"(\\?\C:\a/b\c)");
}

};  // ZEST_SUITE(fs_path_normalize)

}  // namespace

}  // namespace kotatsu::fs::path
