#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <latch>
#include <ranges>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "fs/harness/dir.h"
#include "fs/harness/files.h"
#include "fs/harness/win32.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/fs/directory.h"
#include "kotatsu/fs/file.h"
#include "kotatsu/fs/resolve.h"
#include "kotatsu/fs/status.h"

namespace kotatsu::fs {

namespace {

/// The entries' names, sorted: the system lists them in no set order.
std::vector<std::string> names_of(const std::vector<Directory::Entry>& entries) {
    std::vector<std::string> names;
    for(const auto& entry: entries) {
        names.push_back(entry.name);
    }
    std::ranges::sort(names);
    return names;
}

FileType type_in(const std::vector<Directory::Entry>& entries, std::string_view name) {
    auto found = std::ranges::find(entries, name, &Directory::Entry::name);
    return found == entries.end() ? FileType::Unknown : found->type;
}

/// Runs `run` on a thread of its own, with a stack of 128 KiB.
template <typename Run>
void on_a_small_stack(Run run) {
    constexpr std::size_t stack = 128 * 1024;
#ifdef _WIN32
    auto start = [](void* run) -> DWORD {
        (*static_cast<Run*>(run))();
        return 0;
    };
    HANDLE thread =
        ::CreateThread(nullptr, stack, start, &run, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    ZASSERT(thread != nullptr);
    ::WaitForSingleObject(thread, INFINITE);
    ::CloseHandle(thread);
#else
    auto start = [](void* run) -> void* {
        (*static_cast<Run*>(run))();
        return nullptr;
    };
    pthread_attr_t attributes;
    ZASSERT(::pthread_attr_init(&attributes) == 0);
    ZASSERT(::pthread_attr_setstacksize(&attributes, stack) == 0);
    pthread_t thread;
    auto created = ::pthread_create(&thread, &attributes, start, &run);
    ::pthread_attr_destroy(&attributes);
    ZASSERT(created == 0);
    ZASSERT(::pthread_join(thread, nullptr) == 0);
#endif
}

#ifndef _WIN32
/// The descriptors the process may open cut, while this lives, to `more`:
/// the limit falls after as many free ones, counted from the lowest.
struct ScopedDescriptorLimit {
    explicit ScopedDescriptorLimit(int more) {
        ZASSERT(::getrlimit(RLIMIT_NOFILE, &original) == 0);
        int limit = 0;
        for(int free = 0; free < more; ++limit) {
            if(::fcntl(limit, F_GETFD) == -1 && errno == EBADF) {
                ++free;
            }
        }
        auto lowered = original;
        lowered.rlim_cur = static_cast<rlim_t>(limit);
        ZASSERT(::setrlimit(RLIMIT_NOFILE, &lowered) == 0);
    }

    ScopedDescriptorLimit(const ScopedDescriptorLimit&) = delete;
    ScopedDescriptorLimit& operator=(const ScopedDescriptorLimit&) = delete;

    ~ScopedDescriptorLimit() {
        ::setrlimit(RLIMIT_NOFILE, &original);
    }

    rlimit original;
};
#endif

ZEST_SUITE(fs_directory) {

ZEST_CASE(directory_lists_names_and_types) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    test::write_text(dir.file("é中😀"), "text");
    std::filesystem::create_directory(dir.path / "sub");

    auto entries = test::entries_of(dir.root());
    ZEXPECT(names_of(entries) == std::vector<std::string>{"file.txt", "sub", "é中😀"});
    ZEXPECT(type_in(entries, "file.txt") == FileType::Regular);
    ZEXPECT(type_in(entries, "sub") == FileType::Directory);
}

ZEST_CASE(an_empty_directory_lists_nothing) {
    test::TempDir dir;
    auto directory = Directory::open(dir.root());
    ZASSERT(directory.has_value());
    ZEXPECT(directory->begin() == directory->end());
}

ZEST_CASE(open_of_nothing_fails) {
    test::TempDir dir;
    auto directory = Directory::open(dir.file("missing"));
    ZASSERT(!directory.has_value());
    ZEXPECT(directory.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(open_of_a_file_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    auto directory = Directory::open(dir.file("file.txt"));
    ZASSERT(!directory.has_value());
    ZEXPECT(directory.error() == std::errc::not_a_directory);
}

ZEST_CASE(directory_leaves_links_unfollowed) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
    if(!test::linked(create_directory_symlink("target", dir.file("link")))) {
        return;
    }
    auto entries = test::entries_of(dir.root());
    auto found = std::ranges::find(entries, "link", &Directory::Entry::name);
    ZASSERT(found != entries.end());
    ZEXPECT(found->type == FileType::Symlink);
    auto status = found->status();
    ZASSERT(status.has_value());
    ZEXPECT(status->type == FileType::Symlink);
}

#ifdef _WIN32
ZEST_CASE(directory_lists_a_junction_as_a_link) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
    test::make_junction(dir.file("junction"), dir.file("target"));
    auto entries = test::entries_of(dir.root());
    auto found = std::ranges::find(entries, "junction", &Directory::Entry::name);
    ZASSERT(found != entries.end());
    ZEXPECT(found->type == FileType::Symlink);
    auto status = found->status();
    ZASSERT(status.has_value());
    ZEXPECT(status->type == FileType::Symlink);
}
#else
ZEST_CASE(directory_tells_a_fifo) {
    test::TempDir dir;
    ZASSERT(::mkfifo(dir.file("fifo").c_str(), 0600) == 0);
    auto entries = test::entries_of(dir.root());
    auto found = std::ranges::find(entries, "fifo", &Directory::Entry::name);
    ZASSERT(found != entries.end());
    ZEXPECT(found->type == FileType::Fifo);
    auto status = found->status();
    ZASSERT(status.has_value());
    ZEXPECT(status->type == FileType::Fifo);
}
#endif

ZEST_CASE(entries_tell_their_statuses) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "12345");
    std::filesystem::create_directory(dir.path / "sub");

    auto entries = test::entries_of(dir.root());
    ZASSERT(entries.size() == 2u);
    for(const auto& entry: entries) {
        ZEST_CONTEXT("{}", entry.name);
        auto listed = entry.status();
        auto single = symlink_status(dir.file(entry.name));
        ZASSERT(listed.has_value());
        ZASSERT(single.has_value());
        ZEXPECT(listed->type == entry.type);
        ZEXPECT(listed->type == single->type);
        ZEXPECT(listed->id == single->id);
        ZEXPECT(listed->mtime == single->mtime);
        ZEXPECT(listed->permissions == single->permissions);
        if(entry.type == FileType::Regular) {
            ZEXPECT(listed->size == 5u);
        }
    }
}

ZEST_CASE(entries_tell_their_statuses_once_the_directory_is_gone) {
    // The directory stays open while an entry of it lives.
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "12345");
    auto entries = test::entries_of(dir.root());
    ZASSERT(entries.size() == 1u);
    auto status = entries.front().status();
    ZASSERT(status.has_value());
    ZEXPECT(status->size == 5u);
}

ZEST_CASE(directory_gives_only_what_is_taken) {
    test::TempDir dir;
    std::vector<std::string> names = {"a", "b", "c", "d", "e"};
    for(const auto& name: names) {
        test::write_text(dir.file(name), name);
    }
    for(std::size_t count = 0; count <= names.size() + 1; ++count) {
        ZEST_CONTEXT("count {}", count);
        auto directory = Directory::open(dir.root());
        ZASSERT(directory.has_value());
        // Whichever the system lists first, each once.
        std::vector<std::string> taken;
        for(const auto& entry: *directory | std::views::take(count)) {
            ZASSERT(entry.has_value());
            ZEXPECT(std::ranges::find(names, entry->name) != names.end());
            taken.push_back(entry->name);
        }
        ZEXPECT(taken.size() == std::min(count, names.size()));
        std::ranges::sort(taken);
        ZEXPECT(std::ranges::adjacent_find(taken) == taken.end());
    }
}

#ifdef _WIN32
ZEST_CASE(entries_leave_the_directory_closed) {
    // The listing tells each entry its whole status: once the Directory is
    // gone, nothing holds the directory open, which would keep its parent
    // from being renamed.
    test::TempDir dir;
    std::filesystem::create_directories(dir.path / "outer" / "inner");
    test::write_text(dir.file("outer/inner/file.txt"), "text");
    auto entries = test::entries_of(dir.file("outer/inner"));
    ZASSERT(entries.size() == 1u);
    ZEXPECT(!rename(dir.file("outer"), dir.file("moved")));
}

ZEST_CASE(entry_status_of_a_removed_file_is_as_listed) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "12345");
    auto entries = test::entries_of(dir.root());
    ZASSERT(entries.size() == 1u);
    ZEXPECT(remove(dir.file("file.txt")) == true);
    auto status = entries.front().status();
    ZASSERT(status.has_value());
    ZEXPECT(status->size == 5u);
}
#else
ZEST_CASE(entry_status_of_a_removed_file_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "12345");
    auto entries = test::entries_of(dir.root());
    ZASSERT(entries.size() == 1u);
    ZEXPECT(remove(dir.file("file.txt")) == true);
    auto status = entries.front().status();
    ZASSERT(!status.has_value());
    ZEXPECT(status.error() == std::errc::no_such_file_or_directory);
}
#endif

ZEST_CASE(directory_goes_on_where_it_stopped) {
    // A range to go through once: a second loop takes up the rest.
    test::TempDir dir;
    for(auto name: {"a", "b", "c"}) {
        test::write_text(dir.file(name), name);
    }
    auto directory = Directory::open(dir.root());
    ZASSERT(directory.has_value());
    std::vector<std::string> taken;
    for(const auto& entry: *directory | std::views::take(1)) {
        ZASSERT(entry.has_value());
        taken.push_back(entry->name);
    }
    ZEXPECT(taken.size() == 1u);
    for(const auto& entry: *directory) {
        ZASSERT(entry.has_value());
        taken.push_back(entry->name);
    }
    std::ranges::sort(taken);
    ZEXPECT(taken == std::vector<std::string>{"a", "b", "c"});
}

ZEST_CASE(open_follows_a_link) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
    test::write_text(dir.file("target/file.txt"), "text");
    test::link_directory(dir.file("target"), dir.file("link"));
    auto entries = test::entries_of(dir.file("link"));
    ZEXPECT(names_of(entries) == std::vector<std::string>{"file.txt"});
}

ZEST_CASE(status_tells_the_open_directory) {
    test::TempDir dir;
    auto directory = Directory::open(dir.root());
    ZASSERT(directory.has_value());
    auto opened = directory->status();
    auto named = fs::status(dir.root());
    ZASSERT(opened.has_value());
    ZASSERT(named.has_value());
    ZEXPECT(opened->type == FileType::Directory);
    ZEXPECT(opened->id == named->id);
}

ZEST_CASE(canonical_names_the_open_directory_as_canonical_does) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
    test::link_directory(dir.file("target"), dir.file("link"));
    auto directory = Directory::open(dir.file("link"));
    ZASSERT(directory.has_value());
    std::string opened;
    ZEXPECT(!directory->canonical(opened));
    std::string named;
    ZEXPECT(!fs::canonical(named, dir.file("target")));
    ZEXPECT(opened == named);
}

ZEST_CASE(close_lets_go_of_the_directory) {
    test::TempDir dir;
    test::write_text(dir.file("a"), "12345");
    test::write_text(dir.file("b"), "12345");
    auto directory = Directory::open(dir.root());
    ZASSERT(directory.has_value());
    ZEXPECT(directory->native_handle() != File::closed);
    auto at = directory->begin();
    ZASSERT((*at).has_value());
    const auto& entry = **at;

    directory->close();
    ZEXPECT(directory->native_handle() == File::closed);
    auto status = directory->status();
    ZASSERT(!status.has_value());
    ZEXPECT(status.error() == std::errc::bad_file_descriptor);
    std::string name;
    ZEXPECT(directory->canonical(name) == std::errc::bad_file_descriptor);
    auto volume = directory->volume();
    ZASSERT(!volume.has_value());
    ZEXPECT(volume.error() == std::errc::bad_file_descriptor);
    ZEXPECT(directory->sync() == std::errc::bad_file_descriptor);
    // The entry the range stands on holds the directory still.
    auto listed = entry.status();
    ZASSERT(listed.has_value());
    ZEXPECT(listed->size == 5u);
    // Going on, it gives no more of what it had not given.
    ++at;
    ZASSERT(at != directory->end());
    ZASSERT(!(*at).has_value());
    ZEXPECT((*at).error() == std::errc::bad_file_descriptor);
    ++at;
    ZEXPECT(at == directory->end());
    directory->close();
    ZEXPECT(directory->begin() == directory->end());
}

ZEST_CASE(range_of_a_closed_directory_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    auto directory = Directory::open(dir.root());
    ZASSERT(directory.has_value());
    directory->close();
    auto at = directory->begin();
    ZASSERT(at != directory->end());
    ZASSERT(!(*at).has_value());
    ZEXPECT((*at).error() == std::errc::bad_file_descriptor);
}

#ifdef _WIN32
ZEST_CASE(close_lets_the_directories_above_be_renamed) {
    test::TempDir dir;
    std::filesystem::create_directories(dir.path / "outer" / "inner");
    auto directory = Directory::open(dir.file("outer/inner"));
    ZASSERT(directory.has_value());
    ZEXPECT(rename(dir.file("outer"), dir.file("moved")) == std::errc::permission_denied);
    directory->close();
    ZEXPECT(!rename(dir.file("outer"), dir.file("moved")));
}
#endif

ZEST_CASE(sync_of_a_directory_succeeds) {
    // What a crash would lose no test can tell.
    test::TempDir dir;
    test::write_text(dir.file("new.txt"), "text");
    ZASSERT(!rename(dir.file("new.txt"), dir.file("renamed.txt")));
    auto directory = Directory::open(dir.root());
    ZASSERT(directory.has_value());
    ZEXPECT(!directory->sync());
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
    ZEXPECT(try_exists(dir.file("file.txt")) == false);
    ZEXPECT(try_exists(dir.file("empty")) == false);
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
    auto held = File::open(dir.file("file.txt"));
    ZASSERT(held.has_value());
    ZEXPECT(remove(dir.file("file.txt")) == true);
    // The name is gone at once, and may be taken again.
    ZEXPECT(try_exists(dir.file("file.txt")) == false);
    ZEXPECT(!write_file(dir.file("file.txt"), "again"));
}

ZEST_CASE(remove_takes_a_read_only_file) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    std::filesystem::permissions(dir.path / "file.txt", std::filesystem::perms::owner_read);
    ZEXPECT(remove(dir.file("file.txt")) == true);
}

#ifdef _WIN32
ZEST_CASE(remove_of_a_file_deleted_while_held_open_removes_nothing) {
    // The file is going already; its name stays, and keeps the directory
    // it is in from going, until the last handle goes.
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "tree");
    test::write_text(dir.file("tree/file.txt"), "text");
    test::write_text(dir.file("tree/other.txt"), "text");
    {
        test::DeletePending pending(dir.file("tree/file.txt"));
        ZEXPECT(remove(dir.file("tree/file.txt")) == false);
        ZEXPECT(remove_all(dir.file("tree/file.txt")) == 0u);
        auto removed = remove_all(dir.file("tree"));
        ZASSERT(!removed.has_value());
        ZEXPECT(removed.error() == std::errc::directory_not_empty);
    }
    ZEXPECT(remove_all(dir.file("tree")) == 1u);
}
#endif

ZEST_CASE(remove_all_removes_a_tree) {
    test::TempDir dir;
    std::filesystem::create_directories(dir.path / "tree" / "a" / "b");
    test::write_text(dir.file("tree/a/b/file.txt"), "text");
    test::write_text(dir.file("tree/a/é中.txt"), "text");
    ZEXPECT(remove_all(dir.file("tree")) == 5u);
    ZEXPECT(try_exists(dir.file("tree")) == false);
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

ZEST_CASE(remove_takes_a_junction_not_its_target) {
    // Junctions, unlike symlinks, need no privilege, so CI makes them.
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
    test::write_text(dir.file("target/kept.txt"), "text");
    test::make_junction(dir.file("junction"), dir.file("target"));
    ZEXPECT(remove(dir.file("junction")) == true);
    ZEXPECT(!symlink_status(dir.file("junction")).has_value());
    ZEXPECT(is_file(dir.file("target/kept.txt")));
}

ZEST_CASE(remove_all_of_a_junction_takes_the_junction_alone) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
    test::write_text(dir.file("target/kept.txt"), "text");
    test::make_junction(dir.file("junction"), dir.file("target"));
    ZEXPECT(remove_all(dir.file("junction")) == 1u);
    ZEXPECT(!symlink_status(dir.file("junction")).has_value());
    ZEXPECT(is_file(dir.file("target/kept.txt")));
    // A dangling one too.
    std::filesystem::create_directory(dir.path / "gone");
    test::make_junction(dir.file("dangling"), dir.file("gone"));
    std::filesystem::remove(dir.path / "gone");
    ZEXPECT(remove_all(dir.file("dangling")) == 1u);
    ZEXPECT(!symlink_status(dir.file("dangling")).has_value());
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

ZEST_CASE(remove_all_takes_a_directory_it_may_list_but_not_search) {
    // Empty, it is removed from the directory above, never entered.
    test::TempDir dir;
    std::filesystem::create_directories(dir.path / "tree" / "unsearchable");
    test::ScopedPermissions closed(dir.path / "tree" / "unsearchable",
                                   std::filesystem::perms::owner_read);
    ZEXPECT(remove_all(dir.file("tree")) == 2u);
    ZEXPECT(try_exists(dir.file("tree")) == false);
}

ZEST_CASE(remove_all_removes_a_tree_deeper_than_the_descriptors_left) {
    // A descriptor held for each level would take more than are left.
    test::TempDir dir;
    std::string bottom = dir.file("tree");
    for(int level = 0; level < 32; ++level) {
        bottom += "/d";
    }
    std::filesystem::create_directories(test::std_path(bottom));
    test::write_text(bottom + "/file.txt", "text");
    std::expected<std::uint64_t, std::error_code> removed;
    {
        ScopedDescriptorLimit few(8);
        removed = remove_all(dir.file("tree"));
    }
    ZASSERT(removed.has_value());
    ZEXPECT(*removed == 34u);
}
#endif

ZEST_CASE(remove_all_removes_a_deep_tree) {
    // A thousand levels, made a chain of directories at a time, the tree so
    // far moved to the bottom of each, so that no path the test names grows
    // long; the paths into it outgrow MAX_PATH and macOS's PATH_MAX. macOS
    // and Windows take the longer to remove a name the deeper it lies, which
    // bounds the depth. Removed on a small stack, which a call a level
    // would overflow.
    test::TempDir dir;
    constexpr int chain = 50;
    constexpr int chains = 20;
    std::filesystem::path bottom = "next";
    for(int level = 0; level < chain; ++level) {
        bottom /= "d";
    }
    for(int made = 0; made < chains; ++made) {
        std::filesystem::create_directories(dir.path / bottom);
        if(made > 0) {
            std::filesystem::rename(dir.path / "tree", dir.path / bottom / "tree");
        }
        std::filesystem::rename(dir.path / "next", dir.path / "tree");
    }
    std::expected<std::uint64_t, std::error_code> removed;
    on_a_small_stack([&] { removed = remove_all(dir.file("tree")); });
    ZEXPECT(removed == std::uint64_t(chains * (chain + 1)));
    ZEXPECT(try_exists(dir.file("tree")) == false);
}

ZEST_CASE(remove_of_dots_fails) {
    // Either would remove the directory the path is in: the OS refuses it
    // only once its contents are gone, or on Windows, which resolves the
    // path first, not at all.
    test::TempDir dir;
    std::filesystem::create_directories(dir.path / "tree" / "empty");
    test::write_text(dir.file("tree/kept.txt"), "text");
    for(auto name: {"tree/empty/.", "tree/empty/..", "tree/empty/./", "tree/.", "tree/.."}) {
        ZEST_CONTEXT("{}", name);
        auto removed = remove(dir.file(name));
        ZASSERT(!removed.has_value());
        ZEXPECT(removed.error() == std::errc::invalid_argument);
        auto removed_all = remove_all(dir.file(name));
        ZASSERT(!removed_all.has_value());
        ZEXPECT(removed_all.error() == std::errc::invalid_argument);
    }
    ZEXPECT(is_file(dir.file("tree/kept.txt")));
    ZEXPECT(is_directory(dir.file("tree/empty")));
}

#ifdef _WIN32
ZEST_CASE(remove_of_a_name_win32_trims_away_fails) {
    // Win32 takes trailing dots and spaces off a last name that no separator
    // follows: one made of them alone leaves the directory it is in.
    test::TempDir dir;
    std::filesystem::create_directories(dir.path / "tree" / "empty");
    test::write_text(dir.file("tree/kept.txt"), "text");
    for(auto name: {"...", ". ", " ", ".. ", " ."}) {
        ZEST_CONTEXT("[{}]", name);
        auto removed = remove(dir.file(std::format("tree/empty/{}", name)));
        ZASSERT(!removed.has_value());
        ZEXPECT(removed.error() == std::errc::invalid_argument);
        auto removed_all = remove_all(dir.file(std::format("tree/{}", name)));
        ZASSERT(!removed_all.has_value());
        ZEXPECT(removed_all.error() == std::errc::invalid_argument);
    }
    ZEXPECT(is_file(dir.file("tree/kept.txt")));
    ZEXPECT(is_directory(dir.file("tree/empty")));
    // Followed by a separator, the name is kept: a directory of its own.
    ZASSERT(std::filesystem::create_directory(LR"(\\?\)" + (dir.path / "tree" / "...").wstring()));
    ZEXPECT(remove_all(dir.file("tree/...") + "\\") == 1u);
    ZEXPECT(is_file(dir.file("tree/kept.txt")));
}
#endif

ZEST_CASE(remove_or_rename_of_a_root_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
#ifdef _WIN32
    // A drive no volume has: should the check fail, nothing is there.
    auto drives = ::GetLogicalDrives();
    auto letters = std::views::iota(0, 26) | std::views::reverse;
    auto free = std::ranges::find_if(letters, [&](int i) { return !(drives & (1u << i)); });
    ZASSERT(free != letters.end());
    std::string drive = {static_cast<char>('A' + *free), ':'};
    std::vector<std::string> roots = {drive + R"(\)", drive, R"(\\?\)" + drive + R"(\)"};
#else
    std::vector<std::string> roots = {"/", "//"};
#endif
    std::vector<std::expected<bool, std::error_code>> removed;
    std::vector<std::expected<std::uint64_t, std::error_code>> removed_all;
    std::vector<std::error_code> renamed;
    {
#ifndef _WIN32
        // Should the check fail, no descriptor is left to list the root
        // through, which removing anything in it takes. The checks wait for
        // the limit to go: a sanitizer's runtime takes descriptors too.
        ScopedDescriptorLimit none(0);
#endif
        for(const auto& root: roots) {
            removed.push_back(remove(root));
            removed_all.push_back(remove_all(root));
            renamed.push_back(rename(root, dir.file("moved")));
            renamed.push_back(rename_noreplace(root, dir.file("moved")));
            renamed.push_back(rename(dir.file("file.txt"), root));
            renamed.push_back(rename_noreplace(dir.file("file.txt"), root));
        }
    }
    for(std::size_t i = 0; i < roots.size(); ++i) {
        ZEST_CONTEXT("{}", roots[i]);
        ZASSERT(!removed[i].has_value());
        ZEXPECT(removed[i].error() == std::errc::invalid_argument);
        ZASSERT(!removed_all[i].has_value());
        ZEXPECT(removed_all[i].error() == std::errc::invalid_argument);
        for(std::size_t j = 0; j < 4; ++j) {
            ZEXPECT(renamed[i * 4 + j] == std::errc::invalid_argument);
        }
    }
    ZEXPECT(is_file(dir.file("file.txt")));
}

ZEST_CASE(rename_replaces_a_file) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "new");
    test::write_text(dir.file("to"), "old");
    ZEXPECT(!rename(dir.file("from"), dir.file("to")));
    ZEXPECT(test::read_text(dir.file("to")) == "new");
    ZEXPECT(try_exists(dir.file("from")) == false);
}

ZEST_CASE(rename_replaces_a_file_held_open) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "new");
    test::write_text(dir.file("to"), "old");
    auto held = File::open(dir.file("to"));
    ZASSERT(held.has_value());
    ZEXPECT(!rename(dir.file("from"), dir.file("to")));
    ZEXPECT(test::read_text(dir.file("to")) == "new");
}

ZEST_CASE(read_symlink_gives_the_target_as_written) {
    test::TempDir dir;
    test::write_text(dir.file("target"), "text");
    if(!test::linked(create_symlink("target", dir.file("relative")))) {
        return;
    }
    std::string target;
    ZEXPECT(!read_symlink(target, dir.file("relative")));
    ZEXPECT(target == "target");
    ZEXPECT(!create_symlink(dir.file("target"), dir.file("absolute")));
    ZEXPECT(!read_symlink(target, dir.file("absolute")));
    ZEXPECT(target == dir.file("target"));
    ZEXPECT(!create_symlink("missing", dir.file("dangling")));
    ZEXPECT(!read_symlink(target, dir.file("dangling")));
    ZEXPECT(target == "missing");
    std::filesystem::create_directory(dir.path / "sub");
    test::write_text(dir.file("sub/target"), "text");
    ZEXPECT(!create_symlink("sub/target", dir.file("nested")));
    ZEXPECT(!read_symlink(target, dir.file("nested")));
#ifdef _WIN32
    ZEXPECT(target == R"(sub\target)");
#else
    ZEXPECT(target == "sub/target");
#endif
}

ZEST_CASE(read_symlink_of_a_file_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    std::string target;
    ZEXPECT(read_symlink(target, dir.file("file")) == std::errc::invalid_argument);
    ZEXPECT(read_symlink(target, dir.file("missing")) == std::errc::no_such_file_or_directory);
}

ZEST_CASE(read_symlink_of_a_directory_fails) {
    test::TempDir dir;
    std::string target;
    ZEXPECT(read_symlink(target, dir.root()) == std::errc::invalid_argument);
}

#ifdef _WIN32
ZEST_CASE(read_symlink_gives_the_directory_of_a_junction) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
    test::make_junction(dir.file("junction"), dir.file("target"));
    std::string target;
    ZEXPECT(!read_symlink(target, dir.file("junction")));
    ZEXPECT(target == dir.file("target"));
}

ZEST_CASE(read_symlink_of_a_link_it_cannot_read_fails) {
    // A reparse point a path is resolved through, as Windows containers
    // make: a link, whose target only its own driver knows.
    test::TempDir dir;
    test::write_text(dir.file("link"), "");
    test::set_reparse_point(dir.file("link"), test::container_link_tag);
    auto itself = symlink_status(dir.file("link"));
    ZASSERT(itself.has_value());
    ZEXPECT(itself->type == FileType::Symlink);
    std::string target;
    ZEXPECT(read_symlink(target, dir.file("link")) == std::errc::not_supported);
    // One that holds the file, a WSL fifo, is no link.
    test::write_text(dir.file("fifo"), "");
    test::set_reparse_point(dir.file("fifo"), test::wsl_fifo_tag);
    ZEXPECT(read_symlink(target, dir.file("fifo")) == std::errc::invalid_argument);
}
#endif

ZEST_CASE(create_hard_link_gives_a_file_another_name) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    ZEXPECT(!create_hard_link(dir.file("file"), dir.file("other")));
    ZEXPECT(equivalent(dir.file("file"), dir.file("other")) == true);
    auto found = status(dir.file("other"));
    ZASSERT(found.has_value());
    ZEXPECT(found->links == 2u);
    ZEXPECT(test::read_text(dir.file("other")) == "text");
}

ZEST_CASE(create_hard_link_names_a_symlink_itself) {
    test::TempDir dir;
    test::write_text(dir.file("target"), "text");
    if(!test::linked(create_symlink("target", dir.file("link")))) {
        return;
    }
    ZEXPECT(!create_hard_link(dir.file("link"), dir.file("other")));
    ZEXPECT(is_symlink(dir.file("other")));
    std::string target;
    ZEXPECT(!read_symlink(target, dir.file("other")));
    ZEXPECT(target == "target");
}

ZEST_CASE(create_hard_link_over_a_file_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    test::write_text(dir.file("taken"), "old");
    ZEXPECT(create_hard_link(dir.file("file"), dir.file("taken")) == std::errc::file_exists);
    ZEXPECT(create_hard_link(dir.file("missing"), dir.file("other")) ==
            std::errc::no_such_file_or_directory);
    ZEXPECT(test::read_text(dir.file("taken")) == "old");
}

ZEST_CASE(create_hard_link_to_a_directory_fails) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "sub");
    auto error = create_hard_link(dir.file("sub"), dir.file("other"));
#ifdef _WIN32
    ZEXPECT(error == std::errc::permission_denied);
#else
    ZEXPECT(error == std::errc::operation_not_permitted);
#endif
    ZEXPECT(try_exists(dir.file("other")) == false);
}

ZEST_CASE(rename_of_dots_fails) {
    // As remove(), which takes the name away too: on Windows, which resolves
    // the path first, the directory it is in would move.
    test::TempDir dir;
    std::filesystem::create_directories(dir.path / "tree" / "empty");
    std::filesystem::create_directory(dir.path / "other");
    for(auto name: {"tree/empty/.", "tree/empty/..", "tree/empty/./"}) {
        ZEST_CONTEXT("{}", name);
        ZEXPECT(rename(dir.file(name), dir.file("moved")) == std::errc::invalid_argument);
        ZEXPECT(rename_noreplace(dir.file(name), dir.file("moved")) == std::errc::invalid_argument);
        ZEXPECT(rename(dir.file("other"), dir.file(name)) == std::errc::invalid_argument);
        ZEXPECT(rename_noreplace(dir.file("other"), dir.file(name)) == std::errc::invalid_argument);
    }
    ZEXPECT(is_directory(dir.file("tree/empty")));
    ZEXPECT(is_directory(dir.file("other")));
    ZEXPECT(try_exists(dir.file("moved")) == false);
}

#ifdef _WIN32
ZEST_CASE(rename_of_a_name_win32_trims_away_fails) {
    // As remove(): the name Win32 would take off leaves the directory it is
    // in, which would move.
    test::TempDir dir;
    std::filesystem::create_directories(dir.path / "tree" / "empty");
    std::filesystem::create_directory(dir.path / "other");
    for(auto name: {"...", ". ", " ", ".. ", " ."}) {
        ZEST_CONTEXT("[{}]", name);
        auto trimmed = dir.file(std::format("tree/empty/{}", name));
        ZEXPECT(rename(trimmed, dir.file("moved")) == std::errc::invalid_argument);
        ZEXPECT(rename_noreplace(trimmed, dir.file("moved")) == std::errc::invalid_argument);
        ZEXPECT(rename(dir.file("other"), trimmed) == std::errc::invalid_argument);
        ZEXPECT(rename_noreplace(dir.file("other"), trimmed) == std::errc::invalid_argument);
    }
    ZEXPECT(is_directory(dir.file("tree/empty")));
    ZEXPECT(is_directory(dir.file("other")));
    ZEXPECT(try_exists(dir.file("moved")) == false);
}
#endif

ZEST_CASE(rename_of_nothing_fails) {
    test::TempDir dir;
    ZEXPECT(rename(dir.file("missing"), dir.file("to")) == std::errc::no_such_file_or_directory);
}

ZEST_CASE(rename_noreplace_renames_a_file) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "text");
    ZEXPECT(!rename_noreplace(dir.file("from"), dir.file("é中😀")));
    ZEXPECT(test::read_text(dir.file("é中😀")) == "text");
    ZEXPECT(try_exists(dir.file("from")) == false);
}

ZEST_CASE(rename_noreplace_renames_a_directory) {
    test::TempDir dir;
    std::filesystem::create_directories(dir.path / "from" / "inner");
    ZEXPECT(!rename_noreplace(dir.file("from"), dir.file("to")));
    ZEXPECT(is_directory(dir.file("to/inner")));
    ZEXPECT(try_exists(dir.file("from")) == false);
}

ZEST_CASE(rename_noreplace_renames_a_symlink_itself) {
    test::TempDir dir;
    test::write_text(dir.file("target"), "text");
    if(!test::linked(create_symlink("target", dir.file("link")))) {
        return;
    }
    ZEXPECT(!rename_noreplace(dir.file("link"), dir.file("moved")));
    ZEXPECT(is_symlink(dir.file("moved")));
    ZEXPECT(is_file(dir.file("target")));
}

ZEST_CASE(rename_noreplace_over_a_file_fails) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "new");
    test::write_text(dir.file("to"), "old");
    ZEXPECT(rename_noreplace(dir.file("from"), dir.file("to")) == std::errc::file_exists);
    ZEXPECT(test::read_text(dir.file("to")) == "old");
    ZEXPECT(test::read_text(dir.file("from")) == "new");
}

ZEST_CASE(rename_noreplace_over_a_directory_fails) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "text");
    std::filesystem::create_directory(dir.path / "to");
    ZEXPECT(rename_noreplace(dir.file("from"), dir.file("to")) == std::errc::file_exists);
    ZEXPECT(is_directory(dir.file("to")));
    ZEXPECT(is_file(dir.file("from")));
}

ZEST_CASE(rename_noreplace_over_a_dangling_symlink_fails) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "text");
    if(!test::linked(create_symlink("missing", dir.file("to")))) {
        return;
    }
    ZEXPECT(rename_noreplace(dir.file("from"), dir.file("to")) == std::errc::file_exists);
    ZEXPECT(is_symlink(dir.file("to")));
    ZEXPECT(is_file(dir.file("from")));
}

#ifdef _WIN32
ZEST_CASE(rename_noreplace_over_a_file_deleted_while_held_open_fails) {
    // Its name is taken until the last handle goes.
    test::TempDir dir;
    test::write_text(dir.file("from"), "new");
    test::write_text(dir.file("to"), "old");
    test::DeletePending pending(dir.file("to"));
    ZEXPECT(rename_noreplace(dir.file("from"), dir.file("to")) == std::errc::file_exists);
    ZEXPECT(test::read_text(dir.file("from")) == "new");
}

ZEST_CASE(rename_noreplace_of_a_file_deleted_while_held_open_fails) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "text");
    test::DeletePending pending(dir.file("from"));
    ZEXPECT(rename_noreplace(dir.file("from"), dir.file("to")) ==
            std::errc::no_such_file_or_directory);
    ZEXPECT(try_exists(dir.file("to")) == false);
}
#endif

ZEST_CASE(rename_noreplace_onto_the_file_itself_keeps_it) {
    // By its own name or by another hard link of it, where the systems
    // differ.
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    ZASSERT(!create_hard_link(dir.file("file"), dir.file("other")));
    auto same = rename_noreplace(dir.file("file"), dir.file("file"));
    auto linked = rename_noreplace(dir.file("file"), dir.file("other"));
#if defined(_WIN32)
    ZEXPECT(!same);
    ZEXPECT(!linked);
    ZEXPECT(try_exists(dir.file("file")) == false);
#elif defined(__APPLE__)
    ZEXPECT(!same);
    ZEXPECT(linked == std::errc::file_exists);
    ZEXPECT(is_file(dir.file("file")));
#else
    ZEXPECT(same == std::errc::file_exists);
    ZEXPECT(linked == std::errc::file_exists);
    ZEXPECT(is_file(dir.file("file")));
#endif
    ZEXPECT(test::read_text(dir.file("other")) == "text");
}

ZEST_CASE(rename_noreplace_of_nothing_fails) {
    test::TempDir dir;
    ZEXPECT(rename_noreplace(dir.file("missing"), dir.file("to")) ==
            std::errc::no_such_file_or_directory);
    ZEXPECT(try_exists(dir.file("to")) == false);
}

ZEST_CASE(rename_noreplace_gives_the_name_to_one_of_racing_renames) {
    // Each round, renames of a file of their own to one name, all at once.
    test::TempDir dir;
    constexpr int renamers = 4;
    for(int round = 0; round < 50; ++round) {
        ZEST_CONTEXT("round {}", round);
        auto to = dir.file(std::format("to-{}", round));
        std::array<std::string, renamers> sources;
        for(int i = 0; i < renamers; ++i) {
            sources[i] = dir.file(std::format("from-{}-{}", round, i));
            test::write_text(sources[i], std::to_string(i));
        }
        std::array<std::error_code, renamers> errors;
        std::latch start(renamers);
        std::vector<std::thread> threads;
        for(int i = 0; i < renamers; ++i) {
            threads.emplace_back([&, i] {
                start.arrive_and_wait();
                errors[i] = rename_noreplace(sources[i], to);
            });
        }
        for(auto& thread: threads) {
            thread.join();
        }
        ZEXPECT(std::ranges::count(errors, std::error_code()) == 1);
        for(int i = 0; i < renamers; ++i) {
            ZEST_CONTEXT("renamer {}", i);
            if(errors[i]) {
                ZEXPECT(errors[i] == std::errc::file_exists);
                ZEXPECT(test::read_text(sources[i]) == std::to_string(i));
            } else {
                ZEXPECT(test::read_text(to) == std::to_string(i));
                ZEXPECT(try_exists(sources[i]) == false);
            }
        }
    }
}

#ifdef __linux__
ZEST_CASE(rename_noreplace_across_file_systems_fails) {
    if(!test::across_file_systems()) {
        return;
    }
    test::TempDir dir;
    test::TempDir other("/dev/shm");
    test::write_text(dir.file("from"), "text");
    ZEXPECT(rename_noreplace(dir.file("from"), other.file("to")) == std::errc::cross_device_link);
    ZEXPECT(is_file(dir.file("from")));
    ZEXPECT(try_exists(other.file("to")) == false);
}
#endif

};  // ZEST_SUITE(fs_directory)

}  // namespace

}  // namespace kotatsu::fs
