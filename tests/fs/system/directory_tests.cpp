#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/fs/directory.h"
#include "kota/fs/file.h"
#include "kota/fs/status.h"

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

ZEST_CASE(read_dir_of_a_file_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    auto entries = read_dir(dir.file("file.txt"));
    ZASSERT(!entries.has_value());
    ZEXPECT(entries.error() == std::errc::not_a_directory);
    auto statuses = read_dir_status(dir.file("file.txt"));
    ZASSERT(!statuses.has_value());
    ZEXPECT(statuses.error() == std::errc::not_a_directory);
}

ZEST_CASE(read_dir_leaves_links_unfollowed) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
    if(!test::linked(create_directory_symlink("target", dir.file("link")))) {
        return;
    }
    auto entries = read_dir(dir.root());
    ZASSERT(entries.has_value());
    ZEXPECT(type_in(*entries, "link") == FileType::Symlink);
}

#ifdef _WIN32
ZEST_CASE(read_dir_lists_a_junction_as_a_link) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
    test::make_junction(dir.file("junction"), dir.file("target"));
    auto entries = read_dir(dir.root());
    ZASSERT(entries.has_value());
    ZEXPECT(type_in(*entries, "junction") == FileType::Symlink);
    auto statuses = read_dir_status(dir.root());
    ZASSERT(statuses.has_value());
    auto found = std::ranges::find(*statuses, "junction", &StatusEntry::name);
    ZASSERT(found != statuses->end());
    ZEXPECT(found->status.type == FileType::Symlink);
}
#else
ZEST_CASE(read_dir_tells_a_fifo) {
    test::TempDir dir;
    ZASSERT(::mkfifo(dir.file("fifo").c_str(), 0600) == 0);
    auto entries = read_dir(dir.root());
    ZASSERT(entries.has_value());
    ZEXPECT(type_in(*entries, "fifo") == FileType::Fifo);
}
#endif

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

ZEST_CASE(create_directories_through_a_file_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto made = create_directories(dir.file("file/a/b"));
    ZASSERT(!made.has_value());
    ZEXPECT(made.error() == std::errc::not_a_directory);
    auto itself = create_directories(dir.file("file"));
    ZASSERT(!itself.has_value());
    ZEXPECT(itself.error() == std::errc::file_exists);
}

ZEST_CASE(remove_takes_files_links_and_empty_directories) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    std::filesystem::create_directory(dir.path / "empty");
    ZEXPECT(remove(dir.file("file.txt")) == true);
    ZEXPECT(remove(dir.file("empty")) == true);
    ZEXPECT(remove(dir.file("file.txt")) == false);
    ZEXPECT(exists(dir.file("file.txt")) == false);
    ZEXPECT(exists(dir.file("empty")) == false);
}

ZEST_CASE(remove_under_a_file_removes_nothing) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    ZEXPECT(remove(dir.file("file.txt/child")) == false);
    ZEXPECT(remove_all(dir.file("file.txt/child")) == 0u);
    ZEXPECT(is_file(dir.file("file.txt")));
}

ZEST_CASE(remove_takes_a_link_not_its_target) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
    test::write_text(dir.file("target/kept.txt"), "text");
    if(!test::linked(create_directory_symlink("target", dir.file("link")))) {
        return;
    }
    ZEXPECT(remove(dir.file("link")) == true);
    ZEXPECT(!symlink_status(dir.file("link")).has_value());
    ZEXPECT(is_file(dir.file("target/kept.txt")));
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
    ZEXPECT(exists(dir.file("file.txt")) == false);
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
    ZEXPECT(exists(dir.file("tree")) == false);
    ZEXPECT(remove_all(dir.file("tree")) == 0u);
    test::write_text(dir.file("file.txt"), "text");
    ZEXPECT(remove_all(dir.file("file.txt")) == 1u);
}

ZEST_CASE(remove_all_never_follows_a_link) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "outside");
    test::write_text(dir.file("outside/kept.txt"), "text");
    std::filesystem::create_directory(dir.path / "tree");
    if(!test::linked(create_directory_symlink(dir.file("outside"), dir.file("tree/link")))) {
        return;
    }
    ZEXPECT(remove_all(dir.file("tree")) == 2u);
    ZEXPECT(is_file(dir.file("outside/kept.txt")));
}

ZEST_CASE(remove_all_of_a_link_with_a_separator_removes_the_link) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "outside");
    test::write_text(dir.file("outside/kept.txt"), "text");
    if(!test::linked(create_directory_symlink(dir.file("outside"), dir.file("link")))) {
        return;
    }
    // Through the separator the system would resolve the link.
    ZEXPECT(remove_all(dir.file("link/")) == 1u);
    ZEXPECT(is_file(dir.file("outside/kept.txt")));
    ZEXPECT(!symlink_status(dir.file("link")).has_value());
}

#ifdef _WIN32
ZEST_CASE(remove_all_never_follows_a_junction) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "outside");
    test::write_text(dir.file("outside/kept.txt"), "text");
    std::filesystem::create_directory(dir.path / "tree");
    test::make_junction(dir.file("tree/junction"), dir.file("outside"));
    ZEXPECT(remove_all(dir.file("tree")) == 2u);
    ZEXPECT(is_file(dir.file("outside/kept.txt")));
}
#else
ZEST_CASE(remove_all_where_access_is_denied_fails) {
    test::TempDir dir;
    std::filesystem::create_directories(dir.path / "tree" / "closed");
    test::write_text(dir.file("tree/closed/file.txt"), "text");
    using std::filesystem::perms;
    test::ScopedPermissions closed(dir.path / "tree" / "closed",
                                   perms::owner_read | perms::owner_exec);
    if(::access(dir.file("tree/closed").c_str(), W_OK) == 0) {
        // Root passes through any permission.
        zest::skip();
        return;
    }
    auto removed = remove_all(dir.file("tree"));
    ZASSERT(!removed.has_value());
    ZEXPECT(removed.error() == std::errc::permission_denied);
    ZEXPECT(is_file(dir.file("tree/closed/file.txt")));
}
#endif

ZEST_CASE(remove_all_of_dots_fails) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "tree");
    test::write_text(dir.file("tree/kept.txt"), "text");
    for(auto name: {"tree/.", "tree/..", "tree/./"}) {
        ZEST_CONTEXT("{}", name);
        auto removed = remove_all(dir.file(name));
        ZASSERT(!removed.has_value());
        ZEXPECT(removed.error() == std::errc::invalid_argument);
    }
    ZEXPECT(is_file(dir.file("tree/kept.txt")));
}

ZEST_CASE(rename_replaces_a_file) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "new");
    test::write_text(dir.file("to"), "old");
    ZEXPECT(!rename(dir.file("from"), dir.file("to")));
    ZEXPECT(test::read_text(dir.file("to")) == "new");
    ZEXPECT(exists(dir.file("from")) == false);
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
