#ifdef _WIN32

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "fs/harness/dir.h"
#include "fs/harness/files.h"
#include "fs/harness/win32.h"
#include "kota/zest/zest.h"
#include "kota/fs/directory.h"
#include "kota/fs/file.h"
#include "kota/fs/status.h"
#include "kota/fs/volume.h"

// What NTFS does not show: the calls FAT32 and exFAT refuse, POSIX deletes
// and renames, 128-bit IDs and listings with IDs among them, and the ways
// around them; and, from the system temp directory, another volume.

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
        auto entries = test::entries_of(dir.root());
        ZASSERT(named.has_value());
        ZASSERT(file.has_value());
        ZASSERT(entries.size() == 1u);
        auto opened = file->status();
        auto listed = entries.front().status();
        ZASSERT(opened.has_value());
        ZASSERT(listed.has_value());
        ZEXPECT(named->id.device == serial);
        ZEXPECT(named->id.file != 0u);
        ZEXPECT(opened->id == named->id);
        ZEXPECT(listed->id == named->id);
    });
}

ZEST_CASE(directory_tells_each_entry_its_own_status) {
    // Win32 would take `a.` and `a ` for `a`, which FAT keeps apart.
    on_fat([](const test::TempDir& dir) {
        ZEXPECT(!write_file(verbatim(dir.file("a")), "1"));
        ZEXPECT(!write_file(verbatim(dir.file("a.")), "22"));
        ZEXPECT(!write_file(verbatim(dir.file("a ")), "333"));
        std::vector<std::pair<std::string, std::uint64_t>> sizes;
        for(const auto& entry: test::entries_of(dir.root())) {
            auto status = entry.status();
            ZASSERT(status.has_value());
            ZEXPECT(status->type == FileType::Regular);
            sizes.emplace_back(entry.name, status->size);
        }
        std::ranges::sort(sizes);
        ZEXPECT(sizes == std::vector<std::pair<std::string, std::uint64_t>>{
                             {"a",  1},
                             {"a ", 3},
                             {"a.", 2}
        });
        for(auto name: {"a", "a.", "a "}) {
            ZEXPECT(remove(verbatim(dir.file(name))) == true);
        }
    });
}

ZEST_CASE(an_empty_directory_lists_nothing) {
    // The first read refused for its IDs, the second finds nothing.
    on_fat([](const test::TempDir& dir) {
        auto directory = Directory::open(dir.root());
        ZASSERT(directory.has_value());
        ZEXPECT(directory->begin() == directory->end());
    });
}

ZEST_CASE(entry_status_of_a_removed_file_fails) {
    // FAT lists no whole status: the entry is opened when asked.
    on_fat([](const test::TempDir& dir) {
        test::write_text(dir.file("file.txt"), "12345");
        auto entries = test::entries_of(dir.root());
        ZASSERT(entries.size() == 1u);
        ZEXPECT(remove(dir.file("file.txt")) == true);
        auto status = entries.front().status();
        ZASSERT(!status.has_value());
        ZEXPECT(status.error() == std::errc::no_such_file_or_directory);
    });
}

ZEST_CASE(entry_status_of_a_file_deleted_while_held_open_fails) {
    // FAT lists no whole status: the entry, whose name stays until the last
    // handle goes, is opened when asked.
    on_fat([](const test::TempDir& dir) {
        test::write_text(dir.file("file.txt"), "12345");
        auto entries = test::entries_of(dir.root());
        ZASSERT(entries.size() == 1u);
        test::DeletePending pending(dir.file("file.txt"));
        auto status = entries.front().status();
        ZASSERT(!status.has_value());
        ZEXPECT(status.error() == std::errc::no_such_file_or_directory);
        ZEXPECT(exists(dir.file("file.txt")) == false);
    });
}

ZEST_CASE(set_times_rounds_up_to_two_seconds) {
    // FAT32 and exFAT keep an mtime in two seconds.
    on_fat([](const test::TempDir& dir) {
        test::write_text(dir.file("file.txt"), "text");
        using std::chrono::seconds;
        auto odd = std::chrono::sys_time<std::chrono::nanoseconds>(seconds(1'000'000'001) +
                                                                   std::chrono::milliseconds(500));
        ZEXPECT(!set_times(dir.file("file.txt"), {.mtime = odd}));
        auto found = status(dir.file("file.txt"));
        ZASSERT(found.has_value());
        ZEXPECT(found->mtime ==
                std::chrono::sys_time<std::chrono::nanoseconds>(seconds(1'000'000'002)));
    });
}

ZEST_CASE(volume_names_the_file_system) {
    on_fat([](const test::TempDir& dir) {
        auto named = volume(dir.root());
        auto directory = Directory::open(dir.root());
        ZASSERT(named.has_value());
        ZASSERT(directory.has_value());
        auto listed = directory->volume();
        ZASSERT(listed.has_value());
        ZEXPECT(named->type == listed->type);
        ZEXPECT((named->type == "FAT32" || named->type == "exFAT"));
        ZEXPECT(named->capacity > 0u);
        ZEXPECT(!named->remote);
    });
}

ZEST_CASE(remove_all_removes_a_tree) {
    on_fat([](const test::TempDir& dir) {
        std::filesystem::create_directories(dir.path / "tree" / "inner");
        test::write_text(dir.file("tree/inner/file.txt"), "text");
        test::write_text(dir.file("tree/file.txt"), "text");
        ZEXPECT(remove_all(dir.file("tree")) == 4u);
        ZEXPECT(exists(dir.file("tree")) == false);
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

ZEST_CASE(rename_onto_a_read_only_file_held_open_fails) {
    // The move fails while any handle to the file is open: the attribute
    // taken off for it goes back.
    on_fat([](const test::TempDir& dir) {
        test::write_text(dir.file("new.txt"), "new");
        test::write_text(dir.file("old.txt"), "old");
        make_read_only(dir.file("old.txt"));
        {
            auto held = File::open(dir.file("old.txt"));
            ZASSERT(held.has_value());
            ZEXPECT(rename(dir.file("new.txt"), dir.file("old.txt")) ==
                    std::errc::permission_denied);
        }
        ZEXPECT((test::attributes_of(dir.file("old.txt")) & FILE_ATTRIBUTE_READONLY) != 0);
        ZEXPECT(test::read_text(dir.file("old.txt")) == "old");
        ZEXPECT(test::read_text(dir.file("new.txt")) == "new");
    });
}

ZEST_CASE(write_file_atomic_replaces_the_whole_file) {
    on_fat([](const test::TempDir& dir) {
        test::write_text(dir.file("file.txt"), "old");
        ZEXPECT(!write_file_atomic(dir.file("file.txt"), "new"));
        ZEXPECT(test::read_text(dir.file("file.txt")) == "new");
        ZEXPECT(test::entries_of(dir.root()).size() == 1u);
    });
}

ZEST_CASE(copy_file_copies_from_another_volume) {
    // From the system temp directory's, an mtime FAT keeps whole.
    on_fat([](const test::TempDir& dir) {
        test::TempDir temp;
        auto text = test::text_of(1024 * 1024 + 7);
        test::write_text(temp.file("from"), text);
        auto even =
            std::chrono::sys_time<std::chrono::nanoseconds>(std::chrono::seconds(1'000'000'002));
        ZASSERT(!set_times(temp.file("from"), {.mtime = even}));
        ZEXPECT(!copy_file(temp.file("from"), dir.file("to")));
        ZEXPECT(test::read_text(dir.file("to")) == text);
        auto found = status(dir.file("to"));
        ZASSERT(found.has_value());
        ZEXPECT(found->mtime == even);
    });
}

ZEST_CASE(rename_noreplace_renames_a_file) {
    on_fat([](const test::TempDir& dir) {
        test::write_text(dir.file("from.txt"), "text");
        ZEXPECT(!rename_noreplace(dir.file("from.txt"), dir.file("to.txt")));
        ZEXPECT(test::read_text(dir.file("to.txt")) == "text");
        ZEXPECT(exists(dir.file("from.txt")) == false);
    });
}

ZEST_CASE(rename_noreplace_over_a_file_fails) {
    on_fat([](const test::TempDir& dir) {
        test::write_text(dir.file("from.txt"), "new");
        test::write_text(dir.file("to.txt"), "old");
        ZEXPECT(rename_noreplace(dir.file("from.txt"), dir.file("to.txt")) ==
                std::errc::file_exists);
        ZEXPECT(test::read_text(dir.file("to.txt")) == "old");
        ZEXPECT(test::read_text(dir.file("from.txt")) == "new");
    });
}

ZEST_CASE(rename_noreplace_over_a_file_deleted_while_held_open_fails) {
    // FAT refuses it as it refuses any open of the name, which is taken
    // until the last handle goes.
    on_fat([](const test::TempDir& dir) {
        test::write_text(dir.file("from.txt"), "new");
        test::write_text(dir.file("to.txt"), "old");
        test::DeletePending pending(dir.file("to.txt"));
        ZEXPECT(rename_noreplace(dir.file("from.txt"), dir.file("to.txt")) ==
                std::errc::file_exists);
        ZEXPECT(test::read_text(dir.file("from.txt")) == "new");
    });
}

ZEST_CASE(rename_noreplace_to_another_volume_fails) {
    on_fat([](const test::TempDir& dir) {
        test::TempDir temp;
        test::write_text(temp.file("from"), "text");
        ZEXPECT(rename_noreplace(temp.file("from"), dir.file("to")) ==
                std::errc::cross_device_link);
        ZEXPECT(is_file(temp.file("from")));
        ZEXPECT(exists(dir.file("to")) == false);
    });
}

ZEST_CASE(read_symlink_of_a_file_fails) {
    on_fat([](const test::TempDir& dir) {
        test::write_text(dir.file("file"), "text");
        std::string target;
        ZEXPECT(read_symlink(target, dir.file("file")) == std::errc::invalid_argument);
        ZEXPECT(read_symlink(target, dir.root()) == std::errc::invalid_argument);
    });
}

};  // ZEST_SUITE(fs_fat)

}  // namespace

}  // namespace kota::fs

#endif
