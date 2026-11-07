#include <expected>
#include <filesystem>
#include <string_view>
#include <system_error>

#include "fs/harness/disk_image.h"
#include "fs/harness/files.h"
#include "fs/harness/win32.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/fs/directory.h"

namespace kotatsu::fs {

namespace {

#ifdef _WIN32
/// FileCaseSensitiveInfo, which MinGW's headers declare only for Windows 10
/// 1803 and later (NTDDI_VERSION), above the one they aim at by default.
constexpr auto case_sensitive_info = static_cast<FILE_INFO_BY_HANDLE_CLASS>(23);

/// Marks the directory `path` case-sensitive, as `fsutil file
/// setCaseSensitiveInfo` does; false when the system refuses: Windows keeps
/// the flag on NTFS alone, and some versions set it only where WSL is
/// enabled.
bool mark_case_sensitive(std::string_view path) {
    HANDLE handle = ::CreateFileW(test::std_path(path).wstring().c_str(),
                                  FILE_WRITE_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS,
                                  nullptr);
    ZASSERT(handle != INVALID_HANDLE_VALUE);
    FILE_CASE_SENSITIVE_INFO info{.Flags = FILE_CS_FLAG_CASE_SENSITIVE_DIR};
    bool marked =
        ::SetFileInformationByHandle(handle, case_sensitive_info, &info, sizeof(info)) != FALSE;
    ::CloseHandle(handle);
    return marked;
}
#endif

ZEST_SUITE(fs_directory_case) {

ZEST_CASE(case_sensitive_tells_whether_another_case_names_a_file) {
    test::TempDir dir;
    test::write_text(dir.file("a"), "text");
    auto sensitive = case_sensitive(dir.root());
    ZASSERT(sensitive.has_value());
    ZEXPECT(*sensitive == !std::filesystem::exists(dir.path / "A"));
    // As CI's runners keep their temporary directories: Linux on ext4,
    // macOS on APFS and Windows on NTFS, as each makes them by default.
#ifdef __linux__
    ZEXPECT(*sensitive);
#else
    ZEXPECT(!*sensitive);
#endif
}

// On Windows the target is marked, so that the junction, which is not,
// would tell otherwise.
ZEST_CASE(case_sensitive_follows_a_link) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
#ifdef _WIN32
    if(!mark_case_sensitive(dir.file("target"))) {
        zest::skip();
        return;
    }
#endif
    test::link_directory(dir.file("target"), dir.file("link"));
    auto target = case_sensitive(dir.file("target"));
    auto linked = case_sensitive(dir.file("link"));
    ZASSERT(target.has_value());
    ZASSERT(linked.has_value());
    ZEXPECT(*linked == *target);
#ifdef _WIN32
    ZEXPECT(*linked);
#endif
}

ZEST_CASE(case_sensitive_of_a_file_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto sensitive = case_sensitive(dir.file("file"));
    ZASSERT(!sensitive.has_value());
    ZEXPECT(sensitive.error() == std::errc::not_a_directory);
}

ZEST_CASE(case_sensitive_of_nothing_fails) {
    test::TempDir dir;
    auto sensitive = case_sensitive(dir.file("missing"));
    ZASSERT(!sensitive.has_value());
    ZEXPECT(sensitive.error() == std::errc::no_such_file_or_directory);
}

#ifdef _WIN32
ZEST_CASE(case_sensitive_of_a_directory_marked_so_is_true) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "marked");
    if(!mark_case_sensitive(dir.file("marked"))) {
        zest::skip();
        return;
    }
    ZEXPECT(case_sensitive(dir.file("marked")) == true);
}
#endif

// On Linux a directory marked to fold case (chattr +F) and one on a FAT
// file system are left untested: both need a file system only root mounts
// (ext4 or tmpfs with casefold, a FAT image), which these tests, run as an
// ordinary user, cannot make, and Linux CI mounts none, as Windows CI does
// for fat_tests. They were checked by hand.
#ifdef __linux__
ZEST_CASE(case_sensitive_of_a_file_system_without_flags_is_true) {
    // proc refuses to tell flags of its files (FS_IOC_GETFLAGS), as FAT does,
    // but is none of the FAT family.
    ZEXPECT(case_sensitive("/proc") == true);
}
#endif

#ifdef __APPLE__
ZEST_CASE(case_sensitive_of_a_udf_volume_is_true) {
    // A file system pathconf(_PC_CASE_SENSITIVE) has no answer for.
    test::TempDir dir;
    test::DiskImage image(dir, "UDF");
    std::filesystem::create_directory(test::std_path(image.file("dir")));
    ZEXPECT(case_sensitive(image.file("dir")) == true);
}
#endif

};  // ZEST_SUITE(fs_directory_case)

}  // namespace

}  // namespace kotatsu::fs
