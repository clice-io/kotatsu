#include <string_view>

#include "kotatsu/zest/zest.h"
#include "kotatsu/fs/path.h"

namespace kotatsu::fs::path {

namespace {

constexpr auto posix = Style::Posix;
constexpr auto windows = Style::Windows;

ZEST_SUITE(fs_path_decompose) {

ZEST_CASE(filename_is_the_last_name) {
    ZEXPECT(filename("a/b", posix) == "b");
    ZEXPECT(filename("a/b/", posix) == "b");
    ZEXPECT(filename("a", posix) == "a");
    ZEXPECT(filename("a/..", posix) == "..");
    ZEXPECT(filename("/", posix) == "");
    ZEXPECT(filename("", posix) == "");
    ZEXPECT(filename("a\\b", posix) == "a\\b");
    ZEXPECT(filename("a\\b", windows) == "b");
    ZEXPECT(filename("C:x", windows) == "x");
    ZEXPECT(filename("C:\\", windows) == "");
    ZEXPECT(filename(R"(\\server\share)", windows) == "");
}

ZEST_CASE(parent_drops_the_last_name_and_keeps_the_root) {
    ZEXPECT(parent("a/b/c", posix) == "a/b");
    ZEXPECT(parent("a/b/", posix) == "a");
    ZEXPECT(parent("a//b", posix) == "a");
    ZEXPECT(parent("/a", posix) == "/");
    ZEXPECT(parent("a", posix) == "");
    ZEXPECT(parent("/", posix) == "");
    ZEXPECT(parent("C:\\a", windows) == "C:\\");
    ZEXPECT(parent("C:a", windows) == "C:");
    ZEXPECT(parent("C:\\", windows) == "");
    ZEXPECT(parent(R"(\\server\share\a)", windows) == R"(\\server\share\)");
}

ZEST_CASE(stem_and_extension_split_at_the_last_dot) {
    ZEXPECT(stem("dir/archive.tar.gz", posix) == "archive.tar");
    ZEXPECT(extension("dir/archive.tar.gz", posix) == ".gz");
    ZEXPECT(stem("name.", posix) == "name");
    ZEXPECT(extension("name.", posix) == ".");
    ZEXPECT(stem("name", posix) == "name");
    ZEXPECT(extension("name", posix) == "");
    // A leading dot starts a name, not an extension.
    ZEXPECT(stem(".bashrc", posix) == ".bashrc");
    ZEXPECT(extension(".bashrc", posix) == "");
    ZEXPECT(extension(".config.toml", posix) == ".toml");
    ZEXPECT(stem("..", posix) == "..");
    ZEXPECT(extension("..", posix) == "");
    ZEXPECT(extension("a.b/c", posix) == "");
    ZEXPECT(extension("C:\\dir\\clang.EXE", windows) == ".EXE");
}

};  // ZEST_SUITE(fs_path_decompose)

}  // namespace

}  // namespace kotatsu::fs::path
