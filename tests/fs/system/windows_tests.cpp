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
    ZEXPECT(is_file(file));
    auto entries = read_dir(deep);
    ZASSERT(entries.has_value());
    ZEXPECT(entries->size() == 1u);
    ZEXPECT(remove_all(path::join(dir.root(), "a-directory-name-of-some-length")).has_value());
    ZEXPECT(!is_directory(deep));
}

ZEST_CASE(canonical_spells_the_name_on_disk) {
    test::TempDir dir;
    test::write_text(dir.file("MixedCase.txt"), "text");
    auto real = canonical(dir.file("mixedcase.TXT"));
    ZASSERT(real.has_value());
    ZEXPECT(path::filename(*real) == "MixedCase.txt");
    ZEXPECT(!real->starts_with(R"(\\?\)"));
}

};  // ZEST_SUITE(fs_windows)

}  // namespace

}  // namespace kota::fs::windows

#endif
