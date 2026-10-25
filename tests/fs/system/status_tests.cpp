#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#endif

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/fs/directory.h"
#include "kota/fs/file.h"
#include "kota/fs/status.h"

namespace kota::fs {

namespace {

ZEST_SUITE(fs_status) {

ZEST_CASE(status_tells_files_from_directories) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "12345");

    auto file = status(dir.file("file.txt"));
    ZASSERT(file.has_value());
    ZEXPECT(file->type == FileType::Regular);
    ZEXPECT(file->size == 5);
    ZEXPECT(file->links == 1);

    auto directory = status(dir.root());
    ZASSERT(directory.has_value());
    ZEXPECT(directory->type == FileType::Directory);
    ZEXPECT(directory->id != file->id);
}

ZEST_CASE(status_tells_times_since_the_unix_epoch) {
    test::TempDir dir;
    auto before = std::chrono::system_clock::now();
    test::write_text(dir.file("file.txt"), "text");
    auto found = status(dir.file("file.txt"));
    ZASSERT(found.has_value());
    // File systems keep coarser times than the clock: a minute either way
    // tells a wrong epoch or unit, which would be years off.
    constexpr auto minute = std::chrono::minutes(1);
    ZEXPECT(found->mtime > before - minute);
    ZEXPECT(found->mtime < std::chrono::system_clock::now() + minute);
    ZEXPECT(found->ctime > before - minute);
    ZEXPECT(found->ctime < std::chrono::system_clock::now() + minute);
}

ZEST_CASE(status_tells_a_device) {
#ifdef _WIN32
    auto device = status("nul");
#else
    auto device = status("/dev/null");
#endif
    ZASSERT(device.has_value());
    ZEXPECT(device->type == FileType::Character);
}

#ifndef _WIN32
ZEST_CASE(status_tells_a_fifo) {
    test::TempDir dir;
    ZASSERT(::mkfifo(dir.file("fifo").c_str(), 0600) == 0);
    auto fifo = status(dir.file("fifo"));
    ZASSERT(fifo.has_value());
    ZEXPECT(fifo->type == FileType::Fifo);
}
#endif

ZEST_CASE(status_of_nothing_fails) {
    test::TempDir dir;
    auto missing = status(dir.file("missing"));
    ZASSERT(!missing.has_value());
    ZEXPECT(missing.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(status_takes_names_past_ascii) {
    test::TempDir dir;
    test::write_text(dir.file("é中😀.txt"), "text");
    auto found = status(dir.file("é中😀.txt"));
    ZASSERT(found.has_value());
    ZEXPECT(found->size == 4);
}

ZEST_CASE(one_file_has_one_id) {
    test::TempDir dir;
    test::write_text(dir.file("a.txt"), "a");
    test::write_text(dir.file("b.txt"), "b");
    std::filesystem::create_hard_link(dir.path / "a.txt", dir.path / "link.txt");

    auto a = status(dir.file("a.txt"));
    auto link = status(dir.file("link.txt"));
    ZASSERT(a.has_value());
    ZASSERT(link.has_value());
    ZEXPECT(a->id == link->id);
    ZEXPECT(a->links == 2);
    ZEXPECT(equivalent(dir.file("a.txt"), dir.file("link.txt")) == true);
    ZEXPECT(equivalent(dir.file("a.txt"), dir.file("b.txt")) == false);
}

ZEST_CASE(one_file_has_one_id_by_path_by_handle_and_listed) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    auto named = status(dir.file("file.txt"));
    auto file = File::open(dir.file("file.txt"));
    auto listed = read_dir_status(dir.root());
    ZASSERT(named.has_value());
    ZASSERT(file.has_value());
    ZASSERT(listed.has_value());
    ZASSERT(listed->size() == 1u);
    auto opened = file->status();
    ZASSERT(opened.has_value());
    ZEXPECT(opened->id == named->id);
    ZEXPECT(listed->front().status.id == named->id);
}

#ifdef _WIN32
ZEST_CASE(the_device_is_the_volume_serial_number) {
    // The 32-bit serial every source tells, the open handle's fallback on a
    // volume without 128-bit IDs (FAT) too, as LLVM, libuv and Rust take it.
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    auto volume = dir.path.root_path().wstring();
    DWORD serial = 0;
    ZASSERT(
        ::GetVolumeInformationW(volume.c_str(), nullptr, 0, &serial, nullptr, nullptr, nullptr, 0));
    auto named = status(dir.file("file.txt"));
    auto file = File::open(dir.file("file.txt"));
    auto listed = read_dir_status(dir.root());
    ZASSERT(named.has_value());
    ZASSERT(file.has_value());
    ZASSERT(listed.has_value());
    ZASSERT(listed->size() == 1u);
    auto opened = file->status();
    ZASSERT(opened.has_value());
    ZEXPECT(named->id.device == serial);
    ZEXPECT(opened->id.device == serial);
    ZEXPECT(listed->front().status.id.device == serial);
}
#endif

ZEST_CASE(status_takes_times_past_what_nanoseconds_hold_at_the_nearest) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    using Time = std::chrono::sys_time<std::chrono::nanoseconds>;
#ifdef _WIN32
    // FILETIME counts from 1601, and up to the year 30828.
    auto set_mtime = [&](FILETIME time) {
        auto path = test::std_path(dir.file("file.txt")).wstring();
        HANDLE handle = ::CreateFileW(path.c_str(),
                                      FILE_WRITE_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr,
                                      OPEN_EXISTING,
                                      0,
                                      nullptr);
        ZASSERT(handle != INVALID_HANDLE_VALUE);
        ZASSERT(::SetFileTime(handle, nullptr, nullptr, &time));
        ::CloseHandle(handle);
    };
    set_mtime({.dwLowDateTime = 1, .dwHighDateTime = 0});
    auto early = status(dir.file("file.txt"));
    ZASSERT(early.has_value());
    ZEXPECT(early->mtime == Time::min());
    set_mtime({.dwLowDateTime = 0, .dwHighDateTime = 0x7FFF'0000});
#else
    // 2286, which ext4, XFS, tmpfs and APFS keep, or bring to their latest.
    timespec times[2] = {
        {.tv_sec = 0,              .tv_nsec = UTIME_OMIT},
        {.tv_sec = 10'000'000'000, .tv_nsec = 0         }
    };
    if(::utimensat(AT_FDCWD, dir.file("file.txt").c_str(), times, 0) != 0) {
        // A file system that keeps no time so late refuses it.
        zest::skip();
        return;
    }
#endif
    auto late = status(dir.file("file.txt"));
    ZASSERT(late.has_value());
    ZEXPECT(late->mtime == Time::max());
    auto listed = read_dir_status(dir.root());
    ZASSERT(listed.has_value());
    ZASSERT(listed->size() == 1u);
    ZEXPECT(listed->front().status.mtime == Time::max());
}

ZEST_CASE(status_tells_permissions) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
#ifdef _WIN32
    auto wide = test::std_path(dir.file("file.txt")).wstring();
    ZEXPECT(status(dir.file("file.txt"))->permissions == 0666u);
    ZEXPECT(status(dir.root())->permissions == 0777u);
    ZASSERT(::SetFileAttributesW(wide.c_str(), FILE_ATTRIBUTE_READONLY));
    auto read_only = status(dir.file("file.txt"));
    auto listed = read_dir_status(dir.root());
    ZEXPECT(::SetFileAttributesW(wide.c_str(), FILE_ATTRIBUTE_NORMAL));
    ZASSERT(read_only.has_value());
    ZEXPECT(read_only->permissions == 0444u);
    ZASSERT(listed.has_value());
    ZASSERT(listed->size() == 1u);
    ZEXPECT(listed->front().status.permissions == 0444u);
#else
    using std::filesystem::perms;
    std::filesystem::permissions(dir.path / "file.txt",
                                 perms::owner_read | perms::owner_write | perms::group_read);
    auto file = status(dir.file("file.txt"));
    ZASSERT(file.has_value());
    ZEXPECT(file->permissions == 0640u);
    auto directory = status(dir.root());
    ZASSERT(directory.has_value());
    ZEXPECT(directory->permissions ==
            static_cast<std::uint16_t>(std::filesystem::status(dir.path).permissions()));
#endif
}

ZEST_CASE(equivalent_of_nothing_fails) {
    test::TempDir dir;
    test::write_text(dir.file("a.txt"), "a");
    auto same = equivalent(dir.file("a.txt"), dir.file("missing"));
    ZASSERT(!same.has_value());
    ZEXPECT(same.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(symlink_status_does_not_follow) {
    test::TempDir dir;
    test::write_text(dir.file("target.txt"), "text");
    if(!test::linked(create_symlink("target.txt", dir.file("link")))) {
        return;
    }

    auto followed = status(dir.file("link"));
    ZASSERT(followed.has_value());
    ZEXPECT(followed->type == FileType::Regular);
    ZEXPECT(followed->size == 4);

    auto itself = symlink_status(dir.file("link"));
    ZASSERT(itself.has_value());
    ZEXPECT(itself->type == FileType::Symlink);
    ZEXPECT(is_symlink(dir.file("link")));
    ZEXPECT(!is_symlink(dir.file("target.txt")));
}

ZEST_CASE(symlink_status_of_a_link_and_a_separator_is_the_link) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
#ifdef _WIN32
    test::make_junction(dir.file("link"), dir.file("target"));
#else
    ZASSERT(!create_directory_symlink(dir.file("target"), dir.file("link")));
#endif
    for(auto spelled: {"link/", "link//"}) {
        ZEST_CONTEXT("{}", spelled);
        auto itself = symlink_status(dir.file(spelled));
        ZASSERT(itself.has_value());
        ZEXPECT(itself->type == FileType::Symlink);
        auto followed = status(dir.file(spelled));
        ZASSERT(followed.has_value());
        ZEXPECT(followed->type == FileType::Directory);
    }
    auto directory = symlink_status(dir.file("target/"));
    ZASSERT(directory.has_value());
    ZEXPECT(directory->type == FileType::Directory);
}

ZEST_CASE(a_dangling_symlink_exists_only_itself) {
    test::TempDir dir;
    if(!test::linked(create_symlink("missing", dir.file("link")))) {
        return;
    }
    ZEXPECT(exists(dir.file("link")) == false);
    ZEXPECT(symlink_status(dir.file("link")).has_value());
}

#ifdef _WIN32
ZEST_CASE(junctions_are_links) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
    test::write_text(dir.file("target/file.txt"), "text");
    test::make_junction(dir.file("junction"), dir.file("target"));

    auto itself = symlink_status(dir.file("junction"));
    auto followed = status(dir.file("junction"));
    auto target = status(dir.file("target"));
    ZASSERT(itself.has_value());
    ZASSERT(followed.has_value());
    ZASSERT(target.has_value());
    ZEXPECT(itself->type == FileType::Symlink);
    ZEXPECT(itself->id != target->id);
    ZEXPECT(followed->type == FileType::Directory);
    ZEXPECT(followed->id == target->id);
    ZEXPECT(is_file(dir.file("junction/file.txt")));
}
#endif

#ifdef _WIN32
ZEST_CASE(exists_follows_a_junction) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
    test::make_junction(dir.file("junction"), dir.file("target"));
    ZEXPECT(exists(dir.file("junction")) == true);
    std::filesystem::remove(dir.path / "target");
    ZEXPECT(exists(dir.file("junction")) == false);
    ZEXPECT(symlink_status(dir.file("junction")).has_value());
}
#endif

ZEST_CASE(exists_tells_nothing_from_failure) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    ZEXPECT(exists(dir.file("file.txt")) == true);
    ZEXPECT(exists(dir.file("missing")) == false);
    // A file in place of a directory leaves nothing to find.
    ZEXPECT(exists(dir.file("file.txt/child")) == false);
}

#ifndef _WIN32
ZEST_CASE(exists_where_access_is_denied_fails) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "closed");
    test::write_text(dir.file("closed/file.txt"), "text");
    test::ScopedPermissions closed(dir.path / "closed", std::filesystem::perms::none);
    if(status(dir.file("closed/file.txt"))) {
        // Root passes through any permission.
        zest::skip();
        return;
    }
    auto found = exists(dir.file("closed/file.txt"));
    ZASSERT(!found.has_value());
    ZEXPECT(found.error() == std::errc::permission_denied);
}
#endif

ZEST_CASE(type_checks_are_false_on_failure) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    ZEXPECT(is_file(dir.file("file.txt")));
    ZEXPECT(!is_directory(dir.file("file.txt")));
    ZEXPECT(is_directory(dir.root()));
    ZEXPECT(!is_file(dir.file("missing")));
    ZEXPECT(!is_directory(dir.file("missing")));
    ZEXPECT(!is_symlink(dir.file("missing")));
}

};  // ZEST_SUITE(fs_status)

}  // namespace

}  // namespace kota::fs
