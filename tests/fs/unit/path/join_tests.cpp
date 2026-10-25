#include <string>
#include <string_view>

#include "kota/zest/zest.h"
#include "kota/support/small_string.h"
#include "kota/fs/path.h"

namespace kota::fs::path {

namespace {

constexpr auto posix = Style::Posix;
constexpr auto windows = Style::Windows;

/// `p` pushed onto `base`.
std::string joined(std::string_view base, std::string_view p, Style style) {
    std::string buffer(base);
    push(buffer, p, style);
    return buffer;
}

ZEST_SUITE(fs_path_join) {

ZEST_CASE(push_joins_in_place) {
    std::string p = "a";
    push(p, "b", posix);
    push(p, "c", posix);
    ZEXPECT(p == "a/b/c");
}

ZEST_CASE(push_grows_a_small_string_past_its_inline_capacity) {
    small_string<8> p("/usr");
    push(p, "include/c++/v1", posix);
    ZEXPECT(std::string_view(p) == "/usr/include/c++/v1");
    ZEXPECT(!p.inlined());
}

ZEST_CASE(relative_paths_go_after_the_base) {
    ZEXPECT(joined("a", "b", posix) == "a/b");
    ZEXPECT(joined("a/", "b", posix) == "a/b");
    ZEXPECT(joined("/", "b", posix) == "/b");
    ZEXPECT(joined("", "b", posix) == "b");
    ZEXPECT(joined("a", "", posix) == "a");
    ZEXPECT(joined("a", "../b", posix) == "a/../b");
}

ZEST_CASE(absolute_paths_stand_alone) {
    ZEXPECT(joined("a", "/b", posix) == "/b");
    ZEXPECT(joined("C:\\a", "D:\\b", windows) == "D:\\b");
    ZEXPECT(joined("C:\\a", R"(\\server\share\b)", windows) == R"(\\server\share\b)");
}

ZEST_CASE(windows_joins_keep_the_base_spelling) {
    ZEXPECT(joined("C:\\a", "b", windows) == "C:\\a\\b");
    ZEXPECT(joined("C:/a", "b", windows) == "C:/a/b");
    ZEXPECT(joined("a", "b\\c", windows) == "a\\b\\c");
    ZEXPECT(joined("a", "b", windows) == "a/b");
    ZEXPECT(joined(R"(\\server\share)", "b", windows) == R"(\\server\share\b)");
    ZEXPECT(joined(R"(\\?\C:\a)", "b/c", windows) == R"(\\?\C:\a\b\c)");
}

ZEST_CASE(windows_rooted_paths_keep_the_base_prefix) {
    ZEXPECT(joined("C:\\a", "\\b", windows) == "C:\\b");
    ZEXPECT(joined(R"(\\server\share\a)", "/b", windows) == R"(\\server\share/b)");
    ZEXPECT(joined("a", "\\b", windows) == "\\b");
}

ZEST_CASE(windows_drive_relative_paths_join_on_their_drive) {
    // On its own drive, `C:b` is relative to the base; on another, it is
    // relative to that drive's current directory, which the base is not.
    ZEXPECT(joined("c:\\a", "C:b", windows) == "c:\\a\\b");
    ZEXPECT(joined("D:\\a", "C:b", windows) == "C:b");
    ZEXPECT(joined("a", "C:b", windows) == "C:b");
    ZEXPECT(joined("C:", "b", windows) == "C:b");
}

};  // ZEST_SUITE(fs_path_join)

}  // namespace

}  // namespace kota::fs::path
