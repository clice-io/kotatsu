#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/fs/directory.h"
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
