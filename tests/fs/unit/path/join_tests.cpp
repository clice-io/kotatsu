#include "kota/zest/zest.h"
#include "kota/fs/path.h"

namespace kota::fs::path {

namespace {

constexpr auto posix = Style::Posix;
constexpr auto windows = Style::Windows;

ZEST_SUITE(fs_path_join) {

ZEST_CASE(relative_paths_go_after_the_base) {
    ZEXPECT(join("a", "b", posix) == "a/b");
    ZEXPECT(join("a/", "b", posix) == "a/b");
    ZEXPECT(join("/", "b", posix) == "/b");
    ZEXPECT(join("", "b", posix) == "b");
    ZEXPECT(join("a", "", posix) == "a");
    ZEXPECT(join("a", "../b", posix) == "a/../b");
    ZEXPECT(join({"a", "b", "c"}, posix) == "a/b/c");
}

ZEST_CASE(absolute_paths_stand_alone) {
    ZEXPECT(join("a", "/b", posix) == "/b");
    ZEXPECT(join("C:\\a", "D:\\b", windows) == "D:\\b");
    ZEXPECT(join("C:\\a", R"(\\server\share\b)", windows) == R"(\\server\share\b)");
}

ZEST_CASE(windows_joins_keep_the_base_spelling) {
    ZEXPECT(join("C:\\a", "b", windows) == "C:\\a\\b");
    ZEXPECT(join("C:/a", "b", windows) == "C:/a/b");
    ZEXPECT(join("a", "b\\c", windows) == "a\\b\\c");
    ZEXPECT(join("a", "b", windows) == "a/b");
    ZEXPECT(join(R"(\\server\share)", "b", windows) == R"(\\server\share\b)");
    ZEXPECT(join(R"(\\?\C:\a)", "b/c", windows) == R"(\\?\C:\a\b\c)");
}

ZEST_CASE(windows_rooted_paths_keep_the_base_prefix) {
    ZEXPECT(join("C:\\a", "\\b", windows) == "C:\\b");
    ZEXPECT(join(R"(\\server\share\a)", "/b", windows) == R"(\\server\share/b)");
    ZEXPECT(join("a", "\\b", windows) == "\\b");
}

ZEST_CASE(windows_drive_relative_paths_join_on_their_drive) {
    // On its own drive, `C:b` is relative to the base; on another, it is
    // relative to that drive's current directory, which the base is not.
    ZEXPECT(join("c:\\a", "C:b", windows) == "c:\\a\\b");
    ZEXPECT(join("D:\\a", "C:b", windows) == "C:b");
    ZEXPECT(join("a", "C:b", windows) == "C:b");
    ZEXPECT(join("C:", "b", windows) == "C:b");
}

};  // ZEST_SUITE(fs_path_join)

}  // namespace

}  // namespace kota::fs::path
