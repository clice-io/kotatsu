#include <string>
#include <string_view>

#include "kota/zest/zest.h"
#include "kota/support/small_string.h"
#include "kota/fs/path.h"

namespace kota::fs::path {

namespace {

constexpr auto posix = Style::Posix;
constexpr auto windows = Style::Windows;

/// `p` after set_file_name().
std::string with_file_name(std::string_view p, std::string_view name, Style style) {
    std::string buffer(p);
    set_file_name(buffer, name, style);
    return buffer;
}

/// `p` after set_extension().
std::string with_extension(std::string_view p, std::string_view ext, Style style) {
    std::string buffer(p);
    set_extension(buffer, ext, style);
    return buffer;
}

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

ZEST_CASE(pop_cuts_the_path_to_its_parent) {
    std::string p = "a/b/c";
    ZEXPECT(pop(p, posix));
    ZEXPECT(p == "a/b");
    ZEXPECT(pop(p, posix));
    ZEXPECT(p == "a");
    // One relative name leaves nothing.
    ZEXPECT(pop(p, posix));
    ZEXPECT(p == "");
    p = "/a/";
    ZEXPECT(pop(p, posix));
    ZEXPECT(p == "/");
    p = "C:\\a";
    ZEXPECT(pop(p, windows));
    ZEXPECT(p == "C:\\");
    p = "C:x";
    ZEXPECT(pop(p, windows));
    ZEXPECT(p == "C:");
}

ZEST_CASE(pop_without_a_filename_fails) {
    std::string p = "/";
    ZEXPECT(!pop(p, posix));
    ZEXPECT(p == "/");
    p = "";
    ZEXPECT(!pop(p, posix));
    ZEXPECT(p == "");
    p = "C:";
    ZEXPECT(!pop(p, windows));
    ZEXPECT(p == "C:");
    p = R"(\\server\share)";
    ZEXPECT(!pop(p, windows));
    ZEXPECT(p == R"(\\server\share)");
}

ZEST_CASE(set_file_name_replaces_the_last_name) {
    small_string<8> p("a/b.cpp");
    set_file_name(p, "a-longer-name.h", posix);
    ZEXPECT(std::string_view(p) == "a/a-longer-name.h");
    ZEXPECT(with_file_name("a/b/", "c", posix) == "a/c");
    ZEXPECT(with_file_name("a", "c", posix) == "c");
    ZEXPECT(with_file_name("C:\\a\\b", "c", windows) == "C:\\a\\c");
    // A path without one gains it, as push() joins it.
    ZEXPECT(with_file_name("/", "c", posix) == "/c");
    ZEXPECT(with_file_name("", "c", posix) == "c");
    ZEXPECT(with_file_name("C:", "c", windows) == "C:c");
}

ZEST_CASE(set_extension_swaps_or_removes_it) {
    std::string p = "a/b.cpp";
    ZEXPECT(set_extension(p, ".o", posix));
    ZEXPECT(p == "a/b.o");
    ZEXPECT(with_extension("a/b.cpp", "o", posix) == "a/b.o");
    ZEXPECT(with_extension("a/b", ".o", posix) == "a/b.o");
    ZEXPECT(with_extension("a/b.cpp", "", posix) == "a/b");
    ZEXPECT(with_extension(".bashrc", ".bak", posix) == ".bashrc.bak");
    ZEXPECT(with_extension("a.b/c", "x", posix) == "a.b/c.x");
    ZEXPECT(with_extension("a/b/", ".o", posix) == "a/b.o");
    ZEXPECT(with_extension("C:\\dir\\clang.EXE", ".pdb", windows) == "C:\\dir\\clang.pdb");
}

ZEST_CASE(set_extension_without_a_filename_fails) {
    std::string p = "/";
    ZEXPECT(!set_extension(p, ".o", posix));
    ZEXPECT(p == "/");
    p = "";
    ZEXPECT(!set_extension(p, ".o", posix));
    ZEXPECT(p == "");
    p = "C:\\";
    ZEXPECT(!set_extension(p, ".o", windows));
    ZEXPECT(p == "C:\\");
}

ZEST_CASE(separators_convert_where_they_separate) {
    std::string p = "C:\\a\\b";
    to_slash(p, windows);
    ZEXPECT(p == "C:/a/b");
    p = "a\\b";
    to_slash(p, posix);
    ZEXPECT(p == "a\\b");
    // Past `\\?\` a `/` is a name's character, and `\` must stay.
    p = R"(\\?\C:\a/b)";
    to_slash(p, windows);
    ZEXPECT(p == R"(\\?\C:\a/b)");
    to_backslash(p);
    ZEXPECT(p == R"(\\?\C:\a/b)");
    p = "C:/a/b";
    to_backslash(p);
    ZEXPECT(p == "C:\\a\\b");
}

};  // ZEST_SUITE(fs_path_decompose)

}  // namespace

}  // namespace kota::fs::path
