#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "fs/harness/files.h"
#include "fs/harness/win32.h"
#include "kota/zest/zest.h"
#include "kota/fs/directory.h"
#include "kota/fs/status.h"

namespace kota::fs {

namespace {

using Time = std::chrono::sys_time<std::chrono::nanoseconds>;

/// A time of `nanoseconds` since 1970, past what a second holds, and a
/// multiple of the 100 ns Windows keeps.
constexpr Time at(std::int64_t nanoseconds) {
    return Time(std::chrono::nanoseconds(nanoseconds));
}

constexpr Time first = at(1'000'000'000'123'456'700);
constexpr Time second = at(1'100'000'000'765'432'100);
constexpr Time third = at(1'200'000'000'000'000'000);

/// When `path` was last read, which Status does not tell.
Time atime_of(const std::string& path) {
#ifdef _WIN32
    auto wide = test::std_path(path).wstring();
    WIN32_FILE_ATTRIBUTE_DATA data;
    ZASSERT(::GetFileAttributesExW(wide.c_str(), GetFileExInfoStandard, &data));
    auto ticks = (std::int64_t(data.ftLastAccessTime.dwHighDateTime) << 32) |
                 data.ftLastAccessTime.dwLowDateTime;
    return at((ticks - 116'444'736'000'000'000) * 100);
#else
    struct stat info;
    ZASSERT(::stat(path.c_str(), &info) == 0);
#ifdef __APPLE__
    const auto& time = info.st_atimespec;
#else
    const auto& time = info.st_atim;
#endif
    return at(std::int64_t(time.tv_sec) * 1'000'000'000 + time.tv_nsec);
#endif
}

/// The mtime of `path`.
Time mtime_of(const std::string& path) {
    auto found = status(path);
    ZASSERT(found.has_value());
    return found->mtime;
}

/// The permissions of `path`.
std::uint16_t permissions_of(const std::string& path) {
    auto found = status(path);
    ZASSERT(found.has_value());
    return found->permissions;
}

ZEST_SUITE(fs_status_change) {

ZEST_CASE(set_times_sets_both_times) {
    test::TempDir dir;
    test::write_text(dir.file("é中😀.txt"), "text");
    ZEXPECT(!set_times(dir.file("é中😀.txt"), {.atime = first, .mtime = second}));
    ZEXPECT(atime_of(dir.file("é中😀.txt")) == first);
    ZEXPECT(mtime_of(dir.file("é中😀.txt")) == second);
}

ZEST_CASE(set_times_leaves_a_time_not_given) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    ZEXPECT(!set_times(dir.file("file.txt"), {.atime = first, .mtime = first}));
    ZEXPECT(!set_times(dir.file("file.txt"), {.mtime = second}));
    ZEXPECT(atime_of(dir.file("file.txt")) == first);
    ZEXPECT(mtime_of(dir.file("file.txt")) == second);
    ZEXPECT(!set_times(dir.file("file.txt"), {.atime = third}));
    ZEXPECT(atime_of(dir.file("file.txt")) == third);
    ZEXPECT(mtime_of(dir.file("file.txt")) == second);
}

ZEST_CASE(set_times_sets_a_directory) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "directory");
    ZEXPECT(!set_times(dir.file("directory"), {.mtime = first}));
    ZEXPECT(mtime_of(dir.file("directory")) == first);
}

ZEST_CASE(set_times_follows_a_link) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
#ifdef _WIN32
    test::make_junction(dir.file("link"), dir.file("target"));
#else
    ZASSERT(!create_directory_symlink(dir.file("target"), dir.file("link")));
#endif
    ZEXPECT(!set_times(dir.file("link"), {.mtime = first}));
    ZEXPECT(mtime_of(dir.file("target")) == first);
}

ZEST_CASE(set_times_rounds_down) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    // Before 1970, where rounding toward zero would round up.
    ZEXPECT(!set_times(dir.file("file.txt"), {.mtime = at(-50)}));
#ifdef _WIN32
    ZEXPECT(mtime_of(dir.file("file.txt")) == at(-100));
#else
    ZEXPECT(mtime_of(dir.file("file.txt")) == at(-50));
#endif
}

ZEST_CASE(set_times_takes_the_earliest_time) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    // The file system keeps it, or the earliest time it holds.
    ZEXPECT(!set_times(dir.file("file.txt"), {.mtime = Time::min()}));
    ZEXPECT(mtime_of(dir.file("file.txt")) < at(0));
}

#ifdef __linux__
ZEST_CASE(set_times_keeps_the_earliest_time_on_tmpfs) {
    std::error_code error;
    if(!std::filesystem::is_directory("/dev/shm", error)) {
        // No tmpfs at /dev/shm.
        zest::skip();
        return;
    }
    test::TempDir dir("/dev/shm");
    test::write_text(dir.file("file.txt"), "text");
    ZEXPECT(!set_times(dir.file("file.txt"), {.mtime = Time::min()}));
    ZEXPECT(mtime_of(dir.file("file.txt")) == Time::min());
}
#endif

ZEST_CASE(set_times_of_nothing_fails) {
    test::TempDir dir;
    ZEXPECT(set_times(dir.file("missing"), {.mtime = first}) ==
            std::errc::no_such_file_or_directory);
}

ZEST_CASE(set_permissions_sets_who_may_write) {
    test::TempDir dir;
    test::write_text(dir.file("é中😀.txt"), "text");
    ZEXPECT(!set_permissions(dir.file("é中😀.txt"), 0444));
#ifdef _WIN32
    ZEXPECT(permissions_of(dir.file("é中😀.txt")) == 0444u);
    ZEXPECT((test::attributes_of(dir.file("é中😀.txt")) & FILE_ATTRIBUTE_READONLY) != 0);
    ZEXPECT(!set_permissions(dir.file("é中😀.txt"), 0644));
    ZEXPECT(permissions_of(dir.file("é中😀.txt")) == 0666u);
    ZEXPECT((test::attributes_of(dir.file("é中😀.txt")) & FILE_ATTRIBUTE_READONLY) == 0);
#else
    ZEXPECT(permissions_of(dir.file("é中😀.txt")) == 0444u);
    ZEXPECT(!set_permissions(dir.file("é中😀.txt"), 0755));
    ZEXPECT(permissions_of(dir.file("é中😀.txt")) == 0755u);
    ZEXPECT(std::filesystem::status(dir.path / "é中😀.txt").permissions() ==
            std::filesystem::perms(0755));
#endif
}

#ifdef _WIN32
ZEST_CASE(set_permissions_clears_the_only_attribute) {
    // Attributes of zero would leave them as they are.
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    ZASSERT(::SetFileAttributesW(test::std_path(dir.file("file.txt")).wstring().c_str(),
                                 FILE_ATTRIBUTE_READONLY));
    ZEXPECT(!set_permissions(dir.file("file.txt"), 0666));
    ZEXPECT((test::attributes_of(dir.file("file.txt")) & FILE_ATTRIBUTE_READONLY) == 0);
}

ZEST_CASE(set_permissions_keeps_the_other_attributes) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    ZASSERT(::SetFileAttributesW(test::std_path(dir.file("file.txt")).wstring().c_str(),
                                 FILE_ATTRIBUTE_HIDDEN));
    ZEXPECT(!set_permissions(dir.file("file.txt"), 0444));
    ZEXPECT(test::attributes_of(dir.file("file.txt")) ==
            DWORD{FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY});
    ZEXPECT(!set_permissions(dir.file("file.txt"), 0666));
    ZEXPECT(test::attributes_of(dir.file("file.txt")) == DWORD{FILE_ATTRIBUTE_HIDDEN});
}
#endif

ZEST_CASE(set_permissions_sets_a_directory) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "directory");
    ZEXPECT(!set_permissions(dir.file("directory"), 0555));
    ZEXPECT(permissions_of(dir.file("directory")) == 0555u);
    ZEXPECT(!set_permissions(dir.file("directory"), 0755));
#ifdef _WIN32
    ZEXPECT(permissions_of(dir.file("directory")) == 0777u);
#else
    ZEXPECT(permissions_of(dir.file("directory")) == 0755u);
#endif
}

#ifndef _WIN32
ZEST_CASE(set_permissions_sets_the_special_bits) {
    // Sticky, which a directory's owner may set.
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "directory");
    ZEXPECT(!set_permissions(dir.file("directory"), 01777));
    ZEXPECT(permissions_of(dir.file("directory")) == 01777u);
    ZEXPECT(!set_permissions(dir.file("directory"), 0755));
}
#endif

ZEST_CASE(set_permissions_follows_a_link) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
#ifdef _WIN32
    test::make_junction(dir.file("link"), dir.file("target"));
#else
    ZASSERT(!create_directory_symlink(dir.file("target"), dir.file("link")));
#endif
    ZEXPECT(!set_permissions(dir.file("link"), 0555));
    ZEXPECT(permissions_of(dir.file("target")) == 0555u);
    ZEXPECT(!set_permissions(dir.file("link"), 0755));
}

ZEST_CASE(set_permissions_of_nothing_fails) {
    test::TempDir dir;
    ZEXPECT(set_permissions(dir.file("missing"), 0644) == std::errc::no_such_file_or_directory);
}

};  // ZEST_SUITE(fs_status_change)

}  // namespace

}  // namespace kota::fs
