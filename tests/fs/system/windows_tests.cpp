#ifdef _WIN32

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <windows.h>

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/support/utf16.h"
#include "kota/fs/directory.h"
#include "kota/fs/file.h"
#include "kota/fs/path.h"
#include "kota/fs/resolve.h"
#include "kota/fs/status.h"
#include "kota/fs/windows.h"

namespace kota::fs::windows {

namespace {

/// What to_native() writes for `path`, as UTF-8 for a readable failure.
std::string native(std::string_view path) {
    std::wstring wide;
    if(auto error = to_native(wide, path)) {
        return "error: " + error.message();
    }
    std::string narrow;
    from_utf16(narrow, wide);
    return narrow;
}

/// What from_native() writes for `path`.
std::string plain(std::wstring_view path) {
    std::string out;
    from_native(out, path);
    return out;
}

ZEST_SUITE(fs_windows) {

ZEST_CASE(to_native_leaves_a_short_plain_path) {
    ZEXPECT(native(R"(C:\dir\file.txt)") == R"(C:\dir\file.txt)");
    ZEXPECT(native(R"(\\server\share\file)") == R"(\\server\share\file)");
    ZEXPECT(native(R"(\\?\C:\a\..\b)") == R"(\\?\C:\a\..\b)");
    ZEXPECT(native("C:\\\xC3\xA9") == "C:\\\xC3\xA9");
}

ZEST_CASE(to_native_resolves_what_the_prefix_would_change) {
    std::string cwd;
    ZASSERT(!current_dir(cwd));
    std::wstring relative;
    ZASSERT(!to_native(relative, "a/b"));
    auto joined = cwd;
    path::push(joined, "a\\b");
    path::normalize(joined);
    ZEXPECT(plain(relative) == joined);
    ZEXPECT(native(R"(C:\a\.\b\..\c)") == R"(\\?\C:\a\c)");
    ZEXPECT(native("C:/a/b") == R"(\\?\C:\a\b)");
    // Win32 removes a name's trailing dots and spaces.
    ZEXPECT(native(R"(C:\a\name. .)") == R"(\\?\C:\a\name)");
    ZEXPECT(native(R"(\\server\share\a\..\b)") == R"(\\?\UNC\server\share\b)");
    // A device stays in the device namespace.
    ZEXPECT(native("nul") == R"(\\.\nul)");
}

ZEST_CASE(to_native_resolves_a_path_relative_to_a_drive) {
    std::string cwd;
    ZASSERT(!current_dir(cwd));
    auto drive = cwd.substr(0, 2);
    // A rooted path is on the current directory's drive.
    ZEXPECT(native(R"(\x)") == R"(\\?\)" + drive + R"(\x)");
    // A path relative to the current drive is relative to its directory.
    auto in_cwd = cwd;
    path::push(in_cwd, "x");
    ZEXPECT(native(drive + "x") == R"(\\?\)" + in_cwd);
}

ZEST_CASE(to_native_prefixes_a_long_path) {
    std::string long_path = R"(C:\)" + std::string(300, 'a');
    ZEXPECT(native(long_path) == R"(\\?\)" + long_path);
    // Win32 limits a device path too.
    ZEXPECT(native(R"(\\.\)" + long_path) == R"(\\?\)" + long_path);
}

ZEST_CASE(a_long_device_path_works) {
    test::TempDir dir;
    std::string deep = dir.root();
    while(deep.size() < 300) {
        path::push(deep, "a-directory-name-of-some-length");
    }
    ZEXPECT(create_directories(deep) == true);
    auto file = deep;
    path::push(file, "file.txt");
    ZEXPECT(!write_file(file, "text"));
    std::string content;
    ZEXPECT(!read_file(content, R"(\\.\)" + file));
    ZEXPECT(content == "text");
}

ZEST_CASE(absolute_leaves_a_verbatim_path_as_it_is) {
    test::TempDir dir;
    // Win32 would fold the `..` and drop the dot; the file system keeps
    // both names apart.
    std::string dotted = R"(\\?\)" + dir.root();
    path::push(dotted, "name.");
    ZEXPECT(!write_file(dotted, "dotted"));
    ZEXPECT(!write_file(dir.file("name"), "plain"));
    std::string full;
    ZEXPECT(!absolute(full, dotted));
    ZEXPECT(full == dotted);
    std::string up = R"(\\?\)" + dir.root() + R"(\..\x)";
    ZEXPECT(!absolute(full, up));
    ZEXPECT(full == up);
    std::string real;
    weakly_canonical(real, dotted);
    std::string content;
    ZEXPECT(!read_file(content, real));
    ZEXPECT(content == "dotted");
    ZEXPECT(remove(dotted) == true);
}

ZEST_CASE(to_native_of_what_names_nothing_fails) {
    std::wstring wide;
    ZEXPECT(to_native(wide, std::string_view("a\0b", 3)) == std::errc::invalid_argument);
    ZEXPECT(to_native(wide, "\xFF") == std::errc::illegal_byte_sequence);
}

ZEST_CASE(from_native_drops_the_prefix_where_it_can) {
    ZEXPECT(plain(LR"(\\?\C:\a\b)") == R"(C:\a\b)");
    ZEXPECT(plain(LR"(\\?\UNC\server\share\a)") == R"(\\server\share\a)");
    // Without the prefix the name would lose its trailing dot.
    ZEXPECT(plain(LR"(\\?\C:\a\name.)") == R"(\\?\C:\a\name.)");
    ZEXPECT(plain(LR"(\\?\Volume{1}\a)") == R"(\\?\Volume{1}\a)");
    ZEXPECT(plain(L"\xD800") == "\xED\xA0\x80");
}

ZEST_CASE(errors_map_to_portable_codes) {
    ZEXPECT(to_error_code(ERROR_FILE_NOT_FOUND) == std::errc::no_such_file_or_directory);
    ZEXPECT(to_error_code(ERROR_PATH_NOT_FOUND) == std::errc::no_such_file_or_directory);
    ZEXPECT(to_error_code(ERROR_ACCESS_DENIED) == std::errc::permission_denied);
    ZEXPECT(to_error_code(ERROR_SHARING_VIOLATION) == std::errc::device_or_resource_busy);
    ZEXPECT(to_error_code(ERROR_ALREADY_EXISTS) == std::errc::file_exists);
    ZEXPECT(to_error_code(ERROR_DIR_NOT_EMPTY) == std::errc::directory_not_empty);
    // Mapped by kotatsu, not left to what the standard library knows.
    for(auto code: {ERROR_READ_FAULT, ERROR_WRITE_FAULT}) {
        ZEST_CONTEXT("{}", code);
        auto mapped = to_error_code(code);
        ZEXPECT(mapped.category() == std::generic_category());
        ZEXPECT(mapped == std::errc::io_error);
    }
    // An error with no POSIX counterpart keeps its own code.
    auto own = to_error_code(ERROR_INVALID_EA_NAME);
    ZEXPECT(own.category() == std::system_category());
    ZEXPECT(own.value() == ERROR_INVALID_EA_NAME);
}

ZEST_CASE(paths_longer_than_max_path_work) {
    test::TempDir dir;
    std::string deep = dir.root();
    while(deep.size() < 400) {
        path::push(deep, "a-directory-name-of-some-length");
    }
    ZEXPECT(create_directories(deep) == true);
    auto file = deep;
    path::push(file, "file.txt");
    ZEXPECT(!write_file(file, "text"));
    std::string content;
    ZEXPECT(!read_file(content, file));
    ZEXPECT(content == "text");
    auto found = status(file);
    ZASSERT(found.has_value());
    ZEXPECT(found->size == 4u);
    auto moved = deep;
    path::push(moved, "moved.txt");
    ZEXPECT(!rename(file, moved));
    ZEXPECT(!write_file_atomic(moved, "new"));
    auto entries = read_dir_status(deep);
    ZASSERT(entries.has_value());
    ZASSERT(entries->size() == 1u);
    ZEXPECT(entries->front().name == "moved.txt");
    std::string real;
    ZEXPECT(!canonical(real, moved));
    ZEXPECT(remove(moved) == true);
    ZEXPECT(remove_all(dir.file("a-directory-name-of-some-length")).has_value());
    ZEXPECT(!is_directory(deep));
}

ZEST_CASE(verbatim_paths_reach_names_win32_would_change) {
    test::TempDir dir;
    // Without the prefix Win32 would remove the trailing dot.
    std::string dotted = R"(\\?\)" + dir.root();
    path::push(dotted, "name.");
    ZEXPECT(!write_file(dotted, "text"));
    ZEXPECT(is_file(dotted));
    ZEXPECT(!is_file(dir.file("name")));
    auto entries = read_dir(dir.root());
    ZASSERT(entries.has_value());
    ZASSERT(entries->size() == 1u);
    ZEXPECT(entries->front().name == "name.");
    ZEXPECT(remove(dotted) == true);
}

ZEST_CASE(names_that_are_no_unicode_roundtrip) {
    test::TempDir dir;
    // A lone surrogate, which NTFS takes, in WTF-8.
    auto lone = dir.root();
    path::push(lone, "\xED\xA0\x80");
    ZEXPECT(!write_file(lone, "text"));
    auto entries = read_dir(dir.root());
    ZASSERT(entries.has_value());
    ZASSERT(entries->size() == 1u);
    ZEXPECT(entries->front().name == "\xED\xA0\x80");
    std::string content;
    ZEXPECT(!read_file(content, lone));
    ZEXPECT(content == "text");
    ZEXPECT(remove_all(lone) == 1u);
}

ZEST_CASE(write_file_keeps_a_hidden_file_hidden) {
    test::TempDir dir;
    test::write_text(dir.file("hidden"), "old");
    auto wide = test::std_path(dir.file("hidden")).wstring();
    ZASSERT(::SetFileAttributesW(wide.c_str(), FILE_ATTRIBUTE_HIDDEN));
    ZEXPECT(!write_file(dir.file("hidden"), "new"));
    ZEXPECT(test::read_text(dir.file("hidden")) == "new");
    ZEXPECT((::GetFileAttributesW(wide.c_str()) & FILE_ATTRIBUTE_HIDDEN) != 0u);
}

};  // ZEST_SUITE(fs_windows)

}  // namespace

}  // namespace kota::fs::windows

#endif
