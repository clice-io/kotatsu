#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#endif

#include "fs/harness/dir.h"
#include "fs/harness/files.h"
#include "fs/harness/win32.h"
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
    auto entries = test::entries_of(dir.root());
    ZASSERT(named.has_value());
    ZASSERT(file.has_value());
    ZASSERT(entries.size() == 1u);
    auto opened = file->status();
    auto listed = entries.front().status();
    ZASSERT(opened.has_value());
    ZASSERT(listed.has_value());
    ZEXPECT(opened->id == named->id);
    ZEXPECT(listed->id == named->id);
#ifdef _WIN32
    // The 32-bit serial every source tells, the open handle's fallback on a
    // volume without 128-bit IDs (FAT) too, as LLVM, libuv and Rust take it.
    auto volume = dir.path.root_path().wstring();
    DWORD serial = 0;
    ZASSERT(
        ::GetVolumeInformationW(volume.c_str(), nullptr, 0, &serial, nullptr, nullptr, nullptr, 0));
    ZEXPECT(named->id.device == serial);
#endif
}

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
    struct stat kept;
    if(::utimensat(AT_FDCWD, dir.file("file.txt").c_str(), times, 0) != 0 ||
       ::stat(dir.file("file.txt").c_str(), &kept) != 0 || kept.st_mtime < times[1].tv_sec / 2) {
        // A file system that keeps no time so late refuses it, or brings it
        // back to 2038 (XFS without bigtime, ext4 with small inodes).
        zest::skip();
        return;
    }
#endif
    auto late = status(dir.file("file.txt"));
    ZASSERT(late.has_value());
    ZEXPECT(late->mtime == Time::max());
    auto entries = test::entries_of(dir.root());
    ZASSERT(entries.size() == 1u);
    auto listed = entries.front().status();
    ZASSERT(listed.has_value());
    ZEXPECT(listed->mtime == Time::max());
}

#ifdef __linux__
ZEST_CASE(status_takes_the_earliest_time_nanoseconds_hold) {
    // tmpfs keeps times earlier than nanoseconds since 1970 hold, which
    // disks' file systems do not.
    std::error_code error;
    if(!std::filesystem::is_directory("/dev/shm", error)) {
        // No tmpfs at /dev/shm.
        zest::skip();
        return;
    }
    test::TempDir dir("/dev/shm");
    test::write_text(dir.file("file.txt"), "text");
    // Its seconds alone are past what nanoseconds hold.
    timespec times[2] = {
        {.tv_sec = 0,              .tv_nsec = UTIME_OMIT },
        {.tv_sec = -9'223'372'037, .tv_nsec = 999'999'999}
    };
    struct stat kept;
    ZASSERT(::utimensat(AT_FDCWD, dir.file("file.txt").c_str(), times, 0) == 0);
    ZASSERT(::stat(dir.file("file.txt").c_str(), &kept) == 0);
    if(kept.st_mtim.tv_sec != times[1].tv_sec || kept.st_mtim.tv_nsec != times[1].tv_nsec) {
        // A tmpfs that brings it to the earliest time it keeps.
        zest::skip();
        return;
    }
    auto found = status(dir.file("file.txt"));
    ZASSERT(found.has_value());
    using Time = std::chrono::sys_time<std::chrono::nanoseconds>;
    ZEXPECT(found->mtime == Time(std::chrono::nanoseconds(-9'223'372'036'000'000'001)));
}
#endif

ZEST_CASE(status_tells_permissions) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
#ifdef _WIN32
    auto wide = test::std_path(dir.file("file.txt")).wstring();
    auto file = status(dir.file("file.txt"));
    auto directory = status(dir.root());
    ZASSERT(file.has_value());
    ZASSERT(directory.has_value());
    ZEXPECT(file->permissions == 0666u);
    ZEXPECT(directory->permissions == 0777u);
    ZASSERT(::SetFileAttributesW(wide.c_str(), FILE_ATTRIBUTE_READONLY));
    auto read_only = status(dir.file("file.txt"));
    auto entries = test::entries_of(dir.root());
    ZEXPECT(::SetFileAttributesW(wide.c_str(), FILE_ATTRIBUTE_NORMAL));
    ZASSERT(read_only.has_value());
    ZEXPECT(read_only->permissions == 0444u);
    ZASSERT(entries.size() == 1u);
    auto listed = entries.front().status();
    ZASSERT(listed.has_value());
    ZEXPECT(listed->permissions == 0444u);
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
    test::link_directory(dir.file("target"), dir.file("link"));
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

ZEST_CASE(symlink_status_of_a_link_and_a_separator_is_the_link_wherever_it_leads) {
    test::TempDir dir;
    std::vector<std::string> links = {"dangling/"};
#ifdef _WIN32
    test::make_junction(dir.file("dangling"), dir.file("missing"));
#else
    ZASSERT(!create_directory_symlink(dir.file("missing"), dir.file("dangling")));
    // One that leads to itself, which the system gives up following.
    ZASSERT(!create_symlink("loop", dir.file("loop")));
    links.push_back("loop/");
#endif
    for(const auto& spelled: links) {
        ZEST_CONTEXT("{}", spelled);
        auto itself = symlink_status(dir.file(spelled));
        ZASSERT(itself.has_value());
        ZEXPECT(itself->type == FileType::Symlink);
    }
}

#ifndef _WIN32
ZEST_CASE(symlink_status_of_a_file_link_and_a_separator_fails) {
    // No file is named with a separator after it.
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    ZASSERT(!create_symlink("file.txt", dir.file("link")));
    auto found = symlink_status(dir.file("link/"));
    ZASSERT(!found.has_value());
    ZEXPECT(found.error() == std::errc::not_a_directory);
}
#endif

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
ZEST_CASE(status_tells_a_wsl_fifo_or_device) {
    // WSL keeps one as a reparse point of a tag of its own.
    constexpr std::pair<DWORD, FileType> kinds[] = {
        {0x80000024, FileType::Fifo     },
        {0x80000025, FileType::Character},
        {0x80000026, FileType::Block    },
    };
    test::TempDir dir;
    for(auto [tag, type]: kinds) {
        ZEST_CONTEXT("{:#x}", tag);
        auto file = dir.file(std::format("{:x}", tag));
        test::write_text(file, "");
        test::set_reparse_point(file, tag);
        auto itself = symlink_status(file);
        ZASSERT(itself.has_value());
        ZEXPECT(itself->type == type);
        auto followed = status(file);
        ZASSERT(followed.has_value());
        ZEXPECT(followed->type == type);
    }
}

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
