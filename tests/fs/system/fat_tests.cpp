#ifdef _WIN32

#include <cstdint>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/fs/directory.h"
#include "kota/fs/file.h"
#include "kota/fs/status.h"

// What NTFS does not show: the calls FAT32 and exFAT refuse, POSIX deletes
// and renames, 128-bit IDs and listings with IDs among them, and the ways
// around them.

namespace kota::fs {

namespace {

/// The directories on FAT volumes to test in, KOTA_TEST_FAT_DIRS with `;`
/// between them; Windows CI mounts a FAT32 and an exFAT image there.
std::vector<std::filesystem::path> fat_dirs() {
    wchar_t buffer[1024];
    auto size = ::GetEnvironmentVariableW(L"KOTA_TEST_FAT_DIRS", buffer, 1024);
    std::wstring_view list(buffer, size < 1024 ? size : 0);
    std::vector<std::filesystem::path> dirs;
    for(auto part: std::views::split(list, L';')) {
        if(!part.empty()) {
            dirs.emplace_back(std::wstring(part.begin(), part.end()));
        }
    }
    return dirs;
}

/// Runs `check` with a directory of its own on each FAT volume.
template <typename Check>
void on_fat(Check check) {
    auto dirs = fat_dirs();
    if(dirs.empty()) {
        // No FAT volume mounted: only CI mounts them.
        zest::skip();
        return;
    }
    for(const auto& base: dirs) {
        ZEST_CONTEXT("{}", base.string());
        test::TempDir dir(base);
        check(dir);
    }
}

/// `path` with the verbatim prefix, which keeps the names Win32 rewrites.
std::string verbatim(const std::string& path) {
    return R"(\\?\)" + path;
}

/// Makes the file at `path` read-only.
void make_read_only(const std::string& path) {
    ZASSERT(::SetFileAttributesW(test::std_path(path).wstring().c_str(), FILE_ATTRIBUTE_READONLY));
}

ZEST_SUITE(fs_fat) {

ZEST_CASE(one_file_has_one_id_by_path_by_handle_and_listed) {
    on_fat([](const test::TempDir& dir) {
        test::write_text(dir.file("file.txt"), "text");
        auto volume = dir.path.root_path().wstring();
        DWORD serial = 0;
        ZASSERT(::GetVolumeInformationW(volume.c_str(),
                                        nullptr,
                                        0,
                                        &serial,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        0));
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
        ZEXPECT(named->id.file != 0u);
        ZEXPECT(opened->id == named->id);
        ZEXPECT(listed->front().status.id == named->id);
    });
}

ZEST_CASE(read_dir_status_tells_each_entry_its_own_status) {
    // Win32 would take `a.` and `a ` for `a`.
    on_fat([](const test::TempDir& dir) {
        ZEXPECT(!write_file(verbatim(dir.file("a")), "1"));
        ZEXPECT(!write_file(verbatim(dir.file("a.")), "22"));
        ZEXPECT(!write_file(verbatim(dir.file("a ")), "333"));
        auto listed = read_dir_status(dir.root());
        ZASSERT(listed.has_value());
        ZASSERT(listed->size() == 3u);
        for(const auto& entry: *listed) {
            ZEST_CONTEXT("'{}'", entry.name);
            ZEXPECT(entry.status.type == FileType::Regular);
            ZEXPECT(entry.status.size == (entry.name == "a" ? 1u : entry.name == "a." ? 2u : 3u));
        }
        auto limited = read_dir_status(dir.root(), 2);
        ZASSERT(limited.has_value());
        ZEXPECT(limited->size() == 2u);
        for(auto name: {"a", "a.", "a "}) {
            ZEXPECT(remove(verbatim(dir.file(name))) == true);
        }
    });
}

ZEST_CASE(remove_takes_a_read_only_file) {
    on_fat([](const test::TempDir& dir) {
        test::write_text(dir.file("file.txt"), "text");
        make_read_only(dir.file("file.txt"));
        ZEXPECT(remove(dir.file("file.txt")) == true);
        ZEXPECT(exists(dir.file("file.txt")) == false);
    });
}

ZEST_CASE(remove_all_takes_a_read_only_file) {
    on_fat([](const test::TempDir& dir) {
        std::filesystem::create_directories(dir.path / "tree" / "inner");
        test::write_text(dir.file("tree/inner/file.txt"), "text");
        make_read_only(dir.file("tree/inner/file.txt"));
        ZEXPECT(remove_all(dir.file("tree")) == 3u);
        ZEXPECT(exists(dir.file("tree")) == false);
    });
}

ZEST_CASE(rename_replaces_a_read_only_file) {
    on_fat([](const test::TempDir& dir) {
        test::write_text(dir.file("new.txt"), "new");
        test::write_text(dir.file("old.txt"), "old");
        make_read_only(dir.file("old.txt"));
        ZEXPECT(!rename(dir.file("new.txt"), dir.file("old.txt")));
        ZEXPECT(test::read_text(dir.file("old.txt")) == "new");
        ZEXPECT(exists(dir.file("new.txt")) == false);
    });
}

ZEST_CASE(write_file_atomic_replaces_the_whole_file) {
    on_fat([](const test::TempDir& dir) {
        test::write_text(dir.file("file.txt"), "old");
        ZEXPECT(!write_file_atomic(dir.file("file.txt"), "new"));
        ZEXPECT(test::read_text(dir.file("file.txt")) == "new");
        auto listed = read_dir(dir.root());
        ZASSERT(listed.has_value());
        ZEXPECT(listed->size() == 1u);
    });
}

};  // ZEST_SUITE(fs_fat)

}  // namespace

}  // namespace kota::fs

#endif
