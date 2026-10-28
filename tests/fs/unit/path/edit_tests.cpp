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

/// Whether push() takes `Parts` after the path.
template <typename... Parts>
constexpr bool pushes =
    requires(std::string& path, const Parts&... parts) { push(path, parts...); };

ZEST_SUITE(fs_path_edit) {

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

ZEST_CASE(push_relative_paths_go_after_the_base) {
    ZEXPECT(joined("a", "b", posix) == "a/b");
    ZEXPECT(joined("a/", "b", posix) == "a/b");
    ZEXPECT(joined("/", "b", posix) == "/b");
    ZEXPECT(joined("", "b", posix) == "b");
    ZEXPECT(joined("a", "", posix) == "a");
    ZEXPECT(joined("a", "../b", posix) == "a/../b");
}

ZEST_CASE(push_absolute_paths_stand_alone) {
    ZEXPECT(joined("a", "/b", posix) == "/b");
    ZEXPECT(joined("C:\\a", "D:\\b", windows) == "D:\\b");
    ZEXPECT(joined("C:\\a", R"(\\server\share\b)", windows) == R"(\\server\share\b)");
}

ZEST_CASE(push_keeps_the_windows_base_spelling) {
    ZEXPECT(joined("C:\\a", "b", windows) == "C:\\a\\b");
    ZEXPECT(joined("C:/a", "b", windows) == "C:/a/b");
    ZEXPECT(joined("a", "b\\c", windows) == "a\\b\\c");
    ZEXPECT(joined("a", "b", windows) == "a/b");
    ZEXPECT(joined(R"(\\server\share)", "b", windows) == R"(\\server\share\b)");
    ZEXPECT(joined(R"(\\?\C:\a)", "b/c", windows) == R"(\\?\C:\a\b\c)");
}

ZEST_CASE(push_after_a_bare_windows_prefix_adds_no_separator) {
    ZEXPECT(joined(R"(\\?\UNC\server\)", "x", windows) == R"(\\?\UNC\server\x)");
    ZEXPECT(joined(R"(\\.\)", "x", windows) == R"(\\.\x)");
    ZEXPECT(joined(R"(\\?\)", "x", windows) == R"(\\?\x)");
}

ZEST_CASE(push_rooted_windows_paths_keep_the_base_prefix) {
    ZEXPECT(joined("C:\\a", "\\b", windows) == "C:\\b");
    ZEXPECT(joined(R"(\\server\share\a)", "/b", windows) == R"(\\server\share/b)");
    ZEXPECT(joined("a", "\\b", windows) == "\\b");
}

ZEST_CASE(push_drive_relative_windows_paths_join_on_their_drive) {
    // On its own drive, `C:b` is relative to the base; on another, it is
    // relative to that drive's current directory, which the base is not.
    ZEXPECT(joined("c:\\a", "C:b", windows) == "c:\\a\\b");
    ZEXPECT(joined("D:\\a", "C:b", windows) == "C:b");
    ZEXPECT(joined("a", "C:b", windows) == "C:b");
    ZEXPECT(joined("C:", "b", windows) == "C:b");
}

ZEST_CASE(push_joins_several_names_in_turn) {
    std::string p = "a";
    push(p, "b", std::string("c"), std::string_view("d"), posix);
    ZEXPECT(p == "a/b/c/d");
    p = "C:\\a";
    push(p, "b", "c", windows);
    ZEXPECT(p == "C:\\a\\b\\c");
    // An absolute name takes the place of what came before it.
    p = "a";
    push(p, "b", "/c", "d", posix);
    ZEXPECT(p == "/c/d");
}

ZEST_CASE(push_of_several_names_without_a_style_spells_them_natively) {
    std::string several = "a";
    push(several, "b", "c");
    std::string one_by_one = "a";
    push(one_by_one, "b", native_style);
    push(one_by_one, "c", native_style);
    ZEXPECT(several == one_by_one);
}

ZEST_CASE(push_takes_a_style_only_after_the_last_name) {
    ZSTATIC_EXPECT(pushes<const char*, const char*, Style>);
    ZSTATIC_EXPECT(pushes<std::string, std::string_view, const char*>);
    ZSTATIC_EXPECT(!pushes<Style, const char*, const char*>);
    ZSTATIC_EXPECT(!pushes<const char*, Style, const char*>);
    ZSTATIC_EXPECT(!pushes<const char*, int, const char*>);
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

ZEST_CASE(set_extension_of_dot_or_dot_dot_fails) {
    // With an extension, `.` and `..` would name other files.
    for(std::string_view name: {".", "..", "a/.", "a/../"}) {
        ZEST_CONTEXT("{}", name);
        std::string p(name);
        ZEXPECT(!set_extension(p, ".o", posix));
        ZEXPECT(p == name);
    }
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

ZEST_CASE(lowercase_drive_lowers_the_drive_letter_alone) {
    std::string p = "C:\\A\\B";
    lowercase_drive(p, windows);
    ZEXPECT(p == "c:\\A\\B");
    p = "D:";
    lowercase_drive(p, windows);
    ZEXPECT(p == "d:");
    // With to_slash(), the spelling LSP clients key documents by.
    p = "E:\\src\\a.cpp";
    to_slash(p, windows);
    lowercase_drive(p, windows);
    ZEXPECT(p == "e:/src/a.cpp");
}

ZEST_CASE(lowercase_drive_leaves_other_roots_alone) {
    for(std::string_view written: {R"(\\?\C:\a)", R"(\\Server\Share\A)", R"(\A)"}) {
        ZEST_CONTEXT("{}", written);
        std::string p(written);
        lowercase_drive(p, windows);
        ZEXPECT(p == written);
    }
    // No drive in a POSIX path.
    std::string p = "A:";
    lowercase_drive(p, posix);
    ZEXPECT(p == "A:");
}

};  // ZEST_SUITE(fs_path_edit)

}  // namespace

}  // namespace kota::fs::path
