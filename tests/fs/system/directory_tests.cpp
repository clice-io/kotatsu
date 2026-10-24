#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "fs/harness/files.h"
#include "kota/fs/directory.h"
#include "kota/fs/file.h"
#include "kota/fs/status.h"
#include "kota/zest/zest.h"

namespace kota::fs {

namespace {

/// The entries' names, sorted: the system lists them in no set order.
std::vector<std::string> names_of(const std::vector<Entry>& entries) {
    std::vector<std::string> names;
    for(const auto& entry: entries) {
        names.push_back(entry.name);
    }
    std::ranges::sort(names);
    return names;
}

FileType type_in(const std::vector<Entry>& entries, std::string_view name) {
    auto found = std::ranges::find(entries, name, &Entry::name);
    return found == entries.end() ? FileType::Unknown : found->type;
}

ZEST_SUITE(fs_directory) {

ZEST_CASE(read_dir_lists_names_and_types) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    test::write_text(dir.file("é中😀"), "text");
    std::filesystem::create_directory(dir.path / "sub");

    auto entries = read_dir(dir.root());
    ZASSERT(entries.has_value());
    ZEXPECT(names_of(*entries) == std::vector<std::string>{"file.txt", "sub", "é中😀"});
    ZEXPECT(type_in(*entries, "file.txt") == FileType::Regular);
    ZEXPECT(type_in(*entries, "sub") == FileType::Directory);
}

ZEST_CASE(read_dir_of_an_empty_directory_is_empty) {
    test::TempDir dir;
    auto entries = read_dir(dir.root());
    ZASSERT(entries.has_value());
    ZEXPECT(entries->empty());
}

ZEST_CASE(read_dir_of_nothing_fails) {
    test::TempDir dir;
    auto entries = read_dir(dir.file("missing"));
    ZASSERT(!entries.has_value());
    ZEXPECT(entries.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(read_dir_leaves_links_unfollowed) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
    if(auto error = create_directory_symlink("target", dir.file("link"))) {
        // Windows lets only an administrator or developer mode make links.
        ZEXPECT(error == std::errc::operation_not_permitted);
        zest::skip();
        return;
    }
    auto entries = read_dir(dir.root());
    ZASSERT(entries.has_value());
    ZEXPECT(type_in(*entries, "link") == FileType::Symlink);
}

ZEST_CASE(read_dir_status_matches_each_status) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "12345");
    std::filesystem::create_directory(dir.path / "sub");

    auto entries = read_dir_status(dir.root());
    ZASSERT(entries.has_value());
    ZASSERT(entries->size() == 2);
    for(const auto& entry: *entries) {
        ZEST_CONTEXT("{}", entry.name);
        auto single = symlink_status(dir.file(entry.name));
        ZASSERT(single.has_value());
        ZEXPECT(entry.status.type == single->type);
        ZEXPECT(entry.status.id == single->id);
        ZEXPECT(entry.status.mtime_ns == single->mtime_ns);
        if(entry.status.type == FileType::Regular) {
            ZEXPECT(entry.status.size == 5);
        }
    }
}

ZEST_CASE(create_directory_says_whether_it_made_one) {
    test::TempDir dir;
    ZEXPECT(create_directory(dir.file("new")) == true);
    ZEXPECT(create_directory(dir.file("new")) == false);
    ZEXPECT(is_directory(dir.file("new")));
}

ZEST_CASE(create_directory_over_a_file_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto made = create_directory(dir.file("file"));
    ZASSERT(!made.has_value());
    ZEXPECT(made.error() == std::errc::file_exists);
}

ZEST_CASE(create_directory_without_its_parent_fails) {
    test::TempDir dir;
    auto made = create_directory(dir.file("a/b"));
    ZASSERT(!made.has_value());
    ZEXPECT(made.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(create_directories_makes_every_missing_parent) {
    test::TempDir dir;
    ZEXPECT(create_directories(dir.file("a/b/é中")) == true);
    ZEXPECT(is_directory(dir.file("a/b/é中")));
    ZEXPECT(create_directories(dir.file("a/b/é中")) == false);
    ZEXPECT(create_directories(dir.file("a/c/")) == true);
}

ZEST_CASE(remove_takes_files_links_and_empty_directories) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    std::filesystem::create_directory(dir.path / "empty");
    ZEXPECT(remove(dir.file("file.txt")) == true);
    ZEXPECT(remove(dir.file("empty")) == true);
    ZEXPECT(remove(dir.file("file.txt")) == false);
    ZEXPECT(!exists(dir.file("file.txt")).value());
    ZEXPECT(!exists(dir.file("empty")).value());
}

ZEST_CASE(remove_of_a_full_directory_fails) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "full");
    test::write_text(dir.file("full/file.txt"), "text");
    auto removed = remove(dir.file("full"));
    ZASSERT(!removed.has_value());
    ZEXPECT(removed.error() == std::errc::directory_not_empty);
}

ZEST_CASE(remove_takes_a_file_held_open) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    auto held = File::open(dir.file("file.txt"), OpenMode::Read);
    ZASSERT(held.has_value());
    ZEXPECT(remove(dir.file("file.txt")) == true);
    // The name is gone at once, and may be taken again.
    ZEXPECT(!exists(dir.file("file.txt")).value());
    ZEXPECT(!write_file(dir.file("file.txt"), "again"));
}

ZEST_CASE(remove_takes_a_read_only_file) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    std::filesystem::permissions(dir.path / "file.txt", std::filesystem::perms::owner_read);
    ZEXPECT(remove(dir.file("file.txt")) == true);
}

ZEST_CASE(remove_all_removes_a_tree) {
    test::TempDir dir;
    std::filesystem::create_directories(dir.path / "tree" / "a" / "b");
    test::write_text(dir.file("tree/a/b/file.txt"), "text");
    test::write_text(dir.file("tree/a/é中.txt"), "text");
    ZEXPECT(remove_all(dir.file("tree")) == 5u);
    ZEXPECT(!exists(dir.file("tree")).value());
    ZEXPECT(remove_all(dir.file("tree")) == 0u);
    test::write_text(dir.file("file.txt"), "text");
    ZEXPECT(remove_all(dir.file("file.txt")) == 1u);
}

ZEST_CASE(remove_all_never_follows_a_link) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "outside");
    test::write_text(dir.file("outside/kept.txt"), "text");
    std::filesystem::create_directory(dir.path / "tree");
    if(auto error = create_directory_symlink(dir.file("outside"), dir.file("tree/link"))) {
        ZEXPECT(error == std::errc::operation_not_permitted);
        zest::skip();
        return;
    }
    ZEXPECT(remove_all(dir.file("tree")) == 2u);
    ZEXPECT(is_file(dir.file("outside/kept.txt")));
}

ZEST_CASE(rename_replaces_a_file) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "new");
    test::write_text(dir.file("to"), "old");
    ZEXPECT(!rename(dir.file("from"), dir.file("to")));
    ZEXPECT(test::read_text(dir.file("to")) == "new");
    ZEXPECT(!exists(dir.file("from")).value());
}

ZEST_CASE(rename_replaces_a_file_held_open) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "new");
    test::write_text(dir.file("to"), "old");
    auto held = File::open(dir.file("to"), OpenMode::Read);
    ZASSERT(held.has_value());
    ZEXPECT(!rename(dir.file("from"), dir.file("to")));
    ZEXPECT(test::read_text(dir.file("to")) == "new");
}

ZEST_CASE(rename_of_nothing_fails) {
    test::TempDir dir;
    ZEXPECT(rename(dir.file("missing"), dir.file("to")) == std::errc::no_such_file_or_directory);
}

};  // ZEST_SUITE(fs_directory)

}  // namespace

}  // namespace kota::fs
