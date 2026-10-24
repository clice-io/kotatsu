#ifdef _WIN32

#include <expected>
#include <filesystem>
#include <string>
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

/// What native_path() gave, as UTF-8 for a readable failure.
std::string narrow(const std::expected<std::wstring, std::error_code>& wide) {
    return wide ? from_utf16(*wide) : "error: " + wide.error().message();
}

ZEST_SUITE(fs_windows) {

ZEST_CASE(native_path_leaves_a_short_plain_path) {
    ZEXPECT(narrow(native_path(R"(C:\dir\file.txt)")) == R"(C:\dir\file.txt)");
    ZEXPECT(narrow(native_path(R"(\\server\share\file)")) == R"(\\server\share\file)");
    ZEXPECT(narrow(native_path(R"(\\?\C:\a\..\b)")) == R"(\\?\C:\a\..\b)");
    ZEXPECT(narrow(native_path("C:\\\xC3\xA9")) == "C:\\\xC3\xA9");
}

ZEST_CASE(native_path_resolves_what_the_prefix_would_change) {
    auto cwd = current_dir();
    ZASSERT(cwd.has_value());
    auto relative = native_path("a/b");
    ZASSERT(relative.has_value());
    ZEXPECT(from_native(*relative) == path::normalize(path::join(*cwd, "a\\b")));
    ZEXPECT(narrow(native_path(R"(C:\a\.\b\..\c)")) == R"(\\?\C:\a\c)");
    ZEXPECT(narrow(native_path("C:/a/b")) == R"(\\?\C:\a\b)");
    // Win32 removes a name's trailing dots and spaces.
    ZEXPECT(narrow(native_path(R"(C:\a\name. .)")) == R"(\\?\C:\a\name)");
    ZEXPECT(narrow(native_path(R"(\\server\share\a\..\b)")) == R"(\\?\UNC\server\share\b)");
    // A device stays in the device namespace.
    ZEXPECT(narrow(native_path("nul")) == R"(\\.\nul)");
}

ZEST_CASE(native_path_resolves_a_path_relative_to_a_drive) {
    auto cwd = current_dir();
    ZASSERT(cwd.has_value());
    auto drive = cwd->substr(0, 2);
    // A rooted path is on the current directory's drive.
    ZEXPECT(narrow(native_path(R"(\x)")) == R"(\\?\)" + drive + R"(\x)");
    // A path relative to the current drive is relative to its directory.
    ZEXPECT(narrow(native_path(drive + "x")) == R"(\\?\)" + path::join(*cwd, "x"));
}

ZEST_CASE(native_path_prefixes_a_long_path) {
    std::string long_path = R"(C:\)" + std::string(300, 'a');
    ZEXPECT(narrow(native_path(long_path)) == R"(\\?\)" + long_path);
}

ZEST_CASE(native_path_of_what_names_nothing_fails) {
    auto nul = native_path(std::string_view("a\0b", 3));
    ZASSERT(!nul.has_value());
    ZEXPECT(nul.error() == std::errc::invalid_argument);
    auto invalid = native_path("\xFF");
    ZASSERT(!invalid.has_value());
    ZEXPECT(invalid.error() == std::errc::illegal_byte_sequence);
}

ZEST_CASE(from_native_drops_the_prefix_where_it_can) {
    ZEXPECT(from_native(LR"(\\?\C:\a\b)") == R"(C:\a\b)");
    ZEXPECT(from_native(LR"(\\?\UNC\server\share\a)") == R"(\\server\share\a)");
    // Without the prefix the name would lose its trailing dot.
    ZEXPECT(from_native(LR"(\\?\C:\a\name.)") == R"(\\?\C:\a\name.)");
    ZEXPECT(from_native(LR"(\\?\Volume{1}\a)") == R"(\\?\Volume{1}\a)");
    ZEXPECT(from_native(L"\xD800") == "\xED\xA0\x80");
}

ZEST_CASE(errors_map_to_portable_codes) {
    ZEXPECT(to_error_code(ERROR_FILE_NOT_FOUND) == std::errc::no_such_file_or_directory);
    ZEXPECT(to_error_code(ERROR_PATH_NOT_FOUND) == std::errc::no_such_file_or_directory);
    ZEXPECT(to_error_code(ERROR_ACCESS_DENIED) == std::errc::permission_denied);
    ZEXPECT(to_error_code(ERROR_SHARING_VIOLATION) == std::errc::device_or_resource_busy);
    ZEXPECT(to_error_code(ERROR_ALREADY_EXISTS) == std::errc::file_exists);
    ZEXPECT(to_error_code(ERROR_DIR_NOT_EMPTY) == std::errc::directory_not_empty);
    // An error with no POSIX counterpart keeps its own code.
    auto own = to_error_code(ERROR_INVALID_EA_NAME);
    ZEXPECT(own.category() == std::system_category());
    ZEXPECT(own.value() == ERROR_INVALID_EA_NAME);
}

ZEST_CASE(paths_longer_than_max_path_work) {
    test::TempDir dir;
    std::string deep = dir.root();
    while(deep.size() < 400) {
        deep = path::join(deep, "a-directory-name-of-some-length");
    }
    ZEXPECT(create_directories(deep) == true);
    auto file = path::join(deep, "file.txt");
    ZEXPECT(!write_file(file, "text"));
    ZEXPECT(read_file(file) == std::string("text"));
    auto found = status(file);
    ZASSERT(found.has_value());
    ZEXPECT(found->size == 4u);
    auto moved = path::join(deep, "moved.txt");
    ZEXPECT(!rename(file, moved));
    ZEXPECT(!write_file_atomic(moved, "new"));
    auto entries = read_dir_status(deep);
    ZASSERT(entries.has_value());
    ZASSERT(entries->size() == 1u);
    ZEXPECT(entries->front().name == "moved.txt");
    ZEXPECT(canonical(moved).has_value());
    ZEXPECT(remove(moved) == true);
    ZEXPECT(remove_all(path::join(dir.root(), "a-directory-name-of-some-length")).has_value());
    ZEXPECT(!is_directory(deep));
}

ZEST_CASE(verbatim_paths_reach_names_win32_would_change) {
    test::TempDir dir;
    // Without the prefix Win32 would remove the trailing dot.
    auto dotted = R"(\\?\)" + path::join(dir.root(), "name.");
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
    auto lone = path::join(dir.root(), "\xED\xA0\x80");
    ZEXPECT(!write_file(lone, "text"));
    auto entries = read_dir(dir.root());
    ZASSERT(entries.has_value());
    ZASSERT(entries->size() == 1u);
    ZEXPECT(entries->front().name == "\xED\xA0\x80");
    ZEXPECT(read_file(lone) == std::string("text"));
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
