#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/fs/directory.h"
#include "kota/fs/path.h"

namespace kota::fs {

namespace {

/// What a walk reached of an entry: its path under the root, with `/`
/// whatever the system's separator, and its depth.
struct Reached {
    std::string relative;
    std::size_t depth;
    FileType type;

    friend bool operator==(const Reached&, const Reached&) = default;
};

Reached reached(const Walk::Entry& entry) {
    std::string relative(entry.relative());
    path::to_slash(relative);
    return {.relative = std::move(relative), .depth = entry.depth, .type = entry.type};
}

/// Each entry a walk reaches, in the order it reaches them; each error fails
/// the case.
std::vector<Reached> walked(Walk tree) {
    std::vector<Reached> entries;
    for(auto& entry: tree) {
        ZASSERT(entry.has_value());
        entries.push_back(reached(*entry));
    }
    return entries;
}

/// The paths of `entries`, sorted: the system lists a directory in no set
/// order.
std::vector<std::string> sorted(const std::vector<Reached>& entries) {
    std::vector<std::string> paths;
    for(const auto& entry: entries) {
        paths.push_back(entry.relative);
    }
    std::ranges::sort(paths);
    return paths;
}

/// a/, a/b/, a/b/deep.txt, a/near.txt, top.txt.
void make_tree(const test::TempDir& dir) {
    std::filesystem::create_directories(dir.path / "a" / "b");
    test::write_text(dir.file("a/b/deep.txt"), "deep");
    test::write_text(dir.file("a/near.txt"), "near");
    test::write_text(dir.file("top.txt"), "top");
}

ZEST_SUITE(fs_directory_walk) {

ZEST_CASE(walk_reaches_a_directory_before_what_it_holds) {
    test::TempDir dir;
    make_tree(dir);
    auto entries = walked(walk(dir.root()));
    ZEXPECT(sorted(entries) ==
            std::vector<std::string>{"a", "a/b", "a/b/deep.txt", "a/near.txt", "top.txt"});
    for(std::size_t i = 0; i < entries.size(); ++i) {
        ZEST_CONTEXT("{}", entries[i].relative);
        auto parent = path::parent(entries[i].relative, path::Style::Posix);
        if(!parent.empty()) {
            auto at = std::ranges::find(entries, parent, &Reached::relative);
            ZEXPECT(at < entries.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

ZEST_CASE(walk_tells_depths_and_types) {
    test::TempDir dir;
    make_tree(dir);
    auto entries = walked(walk(dir.root()));
    auto find = [&](std::string_view relative) {
        auto at = std::ranges::find(entries, relative, &Reached::relative);
        ZASSERT(at != entries.end());
        return *at;
    };
    ZEXPECT(find("a") == Reached{.relative = "a", .depth = 1, .type = FileType::Directory});
    ZEXPECT(find("a/b") == Reached{.relative = "a/b", .depth = 2, .type = FileType::Directory});
    ZEXPECT(find("a/b/deep.txt") ==
            Reached{.relative = "a/b/deep.txt", .depth = 3, .type = FileType::Regular});
    ZEXPECT(find("top.txt") ==
            Reached{.relative = "top.txt", .depth = 1, .type = FileType::Regular});
}

ZEST_CASE(walk_entries_tell_their_paths_and_names) {
    test::TempDir dir;
    make_tree(dir);
    for(auto& entry: walk(dir.root())) {
        ZASSERT(entry.has_value());
        ZEST_CONTEXT("{}", entry->path);
        ZEXPECT(entry->name() == path::filename(entry->path));
        std::string under = dir.root();
        path::push(under, entry->relative());
        ZEXPECT(entry->path == under);
    }
}

ZEST_CASE(walk_skips_what_a_skipped_directory_holds) {
    test::TempDir dir;
    make_tree(dir);
    auto tree = walk(dir.root());
    std::vector<Reached> entries;
    for(auto& entry: tree) {
        ZASSERT(entry.has_value());
        entries.push_back(reached(*entry));
        if(entry->name() == "a") {
            tree.skip();
        }
    }
    ZEXPECT(sorted(entries) == std::vector<std::string>{"a", "top.txt"});
}

ZEST_CASE(walk_goes_no_deeper_than_asked) {
    test::TempDir dir;
    make_tree(dir);
    ZEXPECT(sorted(walked(walk(dir.root(), {.max_depth = 1}))) ==
            std::vector<std::string>{"a", "top.txt"});
    ZEXPECT(sorted(walked(walk(dir.root(), {.max_depth = 2}))) ==
            std::vector<std::string>{"a", "a/b", "a/near.txt", "top.txt"});
    ZEXPECT(walked(walk(dir.root(), {.max_depth = 0})).empty());
}

ZEST_CASE(walk_of_an_empty_directory_reaches_nothing) {
    test::TempDir dir;
    ZEXPECT(walked(walk(dir.root())).empty());
}

ZEST_CASE(walk_leaves_links_unfollowed) {
    test::TempDir dir;
    make_tree(dir);
    test::link_directory(dir.file("a"), dir.file("link"));
    auto entries = walked(walk(dir.root()));
    auto link = std::ranges::find(entries, "link", &Reached::relative);
    ZASSERT(link != entries.end());
    ZEXPECT(link->type == FileType::Symlink);
    ZEXPECT(std::ranges::count_if(entries, [](const Reached& entry) {
                return entry.relative.starts_with("link/");
            }) == 0);
}

ZEST_CASE(walk_follows_a_root_that_is_a_link) {
    test::TempDir dir;
    make_tree(dir);
    test::link_directory(dir.file("a"), dir.file("link"));
    ZEXPECT(sorted(walked(walk(dir.file("link")))) ==
            std::vector<std::string>{"b", "b/deep.txt", "near.txt"});
}

ZEST_CASE(walk_of_nothing_fails) {
    test::TempDir dir;
    auto tree = walk(dir.file("missing"));
    auto at = tree.begin();
    ZASSERT(at != tree.end());
    ZASSERT(!(*at).has_value());
    ZEXPECT((*at).error().path == dir.file("missing"));
    ZEXPECT((*at).error().code == std::errc::no_such_file_or_directory);
    ++at;
    ZEXPECT(at == tree.end());
}

ZEST_CASE(walk_of_a_file_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    auto tree = walk(dir.file("file.txt"));
    auto at = tree.begin();
    ZASSERT(at != tree.end());
    ZASSERT(!(*at).has_value());
    ZEXPECT((*at).error().code == std::errc::not_a_directory);
    ++at;
    ZEXPECT(at == tree.end());
}

#ifdef _WIN32
ZEST_CASE(walk_lets_the_directories_above_be_renamed) {
    // Windows keeps a directory with an open handle under it from being
    // renamed: the walk holds none between entries.
    test::TempDir dir;
    make_tree(dir);
    for(auto& entry: walk(dir.file("a"))) {
        ZASSERT(entry.has_value());
        if(entry->name() == "deep.txt") {
            ZEXPECT(!rename(dir.file("a"), dir.file("moved")));
            ZEXPECT(!rename(dir.file("moved"), dir.file("a")));
        }
    }
}
#else
ZEST_CASE(walk_holds_no_descriptor_between_entries) {
    // A descriptor opened deep in the walk takes the lowest free number, the
    // one it takes before.
    test::TempDir dir;
    std::filesystem::create_directories(dir.path / "1" / "2" / "3" / "4" / "5");
    auto lowest = [] {
        int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        ZASSERT(fd >= 0);
        ::close(fd);
        return fd;
    };
    auto before = lowest();
    std::size_t deepest = 0;
    for(auto& entry: walk(dir.root())) {
        ZASSERT(entry.has_value());
        deepest = std::max(deepest, entry->depth);
        ZEXPECT(lowest() == before);
    }
    ZEXPECT(deepest == 5);
}

ZEST_CASE(walk_reports_a_directory_it_cannot_read_and_goes_on) {
    test::TempDir dir;
    std::filesystem::create_directories(dir.path / "closed" / "hidden");
    std::filesystem::create_directories(dir.path / "open");
    test::write_text(dir.file("open/file.txt"), "text");
    test::ScopedPermissions closed(dir.path / "closed", std::filesystem::perms::none);
    if(Directory::open(dir.file("closed"))) {
        // Root reads any directory.
        zest::skip();
        return;
    }
    std::vector<std::string> reached;
    std::vector<std::string> failed;
    for(auto& entry: walk(dir.root())) {
        if(entry) {
            reached.emplace_back(entry->relative());
        } else {
            ZEXPECT(entry.error().code == std::errc::permission_denied);
            failed.push_back(entry.error().path);
        }
    }
    std::ranges::sort(reached);
    ZEXPECT(reached == std::vector<std::string>{"closed", "open", "open/file.txt"});
    ZEXPECT(failed == std::vector<std::string>{dir.file("closed")});
}
#endif

};  // ZEST_SUITE(fs_directory_walk)

}  // namespace

}  // namespace kota::fs
