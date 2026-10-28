#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#include "fs/harness/dir.h"
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

ZEST_CASE(walk_reaches_what_a_directory_holds_right_after_it) {
    // Depth first: nothing else comes between.
    test::TempDir dir;
    make_tree(dir);
    std::filesystem::create_directories(dir.path / "c" / "d");
    test::write_text(dir.file("c/d/file.txt"), "text");
    auto entries = walked(walk(dir.root()));
    for(std::size_t i = 0; i < entries.size(); ++i) {
        if(entries[i].type != FileType::Directory) {
            continue;
        }
        ZEST_CONTEXT("{}", entries[i].relative);
        auto prefix = entries[i].relative + "/";
        auto under = [&](const Reached& entry) {
            return entry.relative.starts_with(prefix);
        };
        auto held = std::ranges::count_if(entries, under);
        auto next = entries.begin() + static_cast<std::ptrdiff_t>(i) + 1;
        auto run = std::ranges::find_if_not(next, entries.end(), under) - next;
        ZEXPECT(run == held);
    }
}

ZEST_CASE(walk_reaches_the_entries_of_a_directory_in_the_order_listed) {
    test::TempDir dir;
    for(int i = 0; i < 10; ++i) {
        test::write_text(dir.file(std::format("file{}.txt", i)), "text");
    }
    std::vector<std::string> listed;
    for(const auto& entry: test::entries_of(dir.root())) {
        listed.push_back(entry.name);
    }
    std::vector<std::string> reached;
    for(const auto& entry: walked(walk(dir.root()))) {
        reached.push_back(entry.relative);
    }
    ZEXPECT(reached == listed);
}

ZEST_CASE(walk_goes_on_once_an_entry_is_moved_out) {
    test::TempDir dir;
    make_tree(dir);
    std::vector<Walk::Entry> moved;
    for(auto& entry: walk(dir.root())) {
        ZASSERT(entry.has_value());
        moved.push_back(std::move(*entry));
    }
    ZEXPECT(moved.size() == 5u);
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
    std::size_t count = 0;
    for(auto& entry: walk(dir.root())) {
        ZASSERT(entry.has_value());
        ZEST_CONTEXT("{}", entry->path);
        ++count;
        ZEXPECT(entry->name() == path::filename(entry->path));
        std::string under = dir.root();
        path::push(under, entry->relative());
        ZEXPECT(entry->path == under);
    }
    ZEXPECT(count == 5u);
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

ZEST_CASE(walk_skips_only_the_directory_it_stands_on) {
    test::TempDir dir;
    for(auto name: {"x", "y"}) {
        std::filesystem::create_directory(dir.path / name);
        test::write_text(dir.file(std::format("{}/f.txt", name)), "text");
    }
    // At a file, skip() does nothing, and leaves nothing for the next
    // directory.
    auto tree = walk(dir.root());
    std::vector<Reached> entries;
    for(auto& entry: tree) {
        ZASSERT(entry.has_value());
        entries.push_back(reached(*entry));
        if(entry->type == FileType::Regular) {
            tree.skip();
        }
    }
    ZEXPECT(sorted(entries) == std::vector<std::string>{"x", "x/f.txt", "y", "y/f.txt"});
    // At the first directory, the second is walked into all the same.
    auto again = walk(dir.root());
    std::string skipped;
    entries.clear();
    for(auto& entry: again) {
        ZASSERT(entry.has_value());
        entries.push_back(reached(*entry));
        if(skipped.empty() && entry->type == FileType::Directory) {
            skipped = entry->name();
            again.skip();
        }
    }
    std::vector<std::string> expected{"x",
                                      "y",
                                      std::format("{}/f.txt", skipped == "x" ? "y" : "x")};
    std::ranges::sort(expected);
    ZEXPECT(sorted(entries) == expected);
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
    ZEXPECT((*at).error().path == dir.file("file.txt"));
    ZEXPECT((*at).error().code == std::errc::not_a_directory);
    ++at;
    ZEXPECT(at == tree.end());
}

ZEST_CASE(walk_of_a_directory_gone_since_it_was_listed_fails_and_goes_on) {
    test::TempDir dir;
    make_tree(dir);
    std::vector<std::string> reached;
    std::vector<std::string> failed;
    for(auto& entry: walk(dir.root())) {
        if(!entry) {
            ZEXPECT(entry.error().code == std::errc::no_such_file_or_directory);
            failed.push_back(entry.error().path);
            continue;
        }
        std::string relative(entry->relative());
        path::to_slash(relative);
        reached.push_back(relative);
        if(relative == "a/b") {
            std::filesystem::remove_all(dir.path / "a" / "b");
        }
    }
    std::ranges::sort(reached);
    ZEXPECT(reached == std::vector<std::string>{"a", "a/b", "a/near.txt", "top.txt"});
    std::string gone = dir.file("a");
    path::push(gone, "b");
    ZEXPECT(failed == std::vector<std::string>{gone});
}

#ifdef _WIN32
ZEST_CASE(walk_lets_the_directories_above_be_renamed) {
    // Windows keeps a directory with an open handle under it from being
    // renamed: the walk holds none between entries.
    test::TempDir dir;
    make_tree(dir);
    bool renamed = false;
    for(auto& entry: walk(dir.file("a"))) {
        ZASSERT(entry.has_value());
        if(entry->name() == "deep.txt") {
            ZEXPECT(!rename(dir.file("a"), dir.file("moved")));
            ZEXPECT(!rename(dir.file("moved"), dir.file("a")));
            renamed = true;
        }
    }
    ZEXPECT(renamed);
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

ZEST_CASE(walk_of_a_directory_it_cannot_read_fails_and_goes_on) {
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
