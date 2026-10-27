#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "fs/harness/files.h"
#include "fs/harness/win32.h"
#include "kota/zest/zest.h"
#include "kota/fs/directory.h"
#include "kota/fs/file.h"
#include "kota/fs/status.h"

namespace kota::fs {

namespace {

using Time = std::chrono::sys_time<std::chrono::nanoseconds>;

/// A time past what a second holds, and a multiple of the 100 ns Windows
/// keeps.
constexpr Time past = Time(std::chrono::nanoseconds(1'000'000'000'123'456'700));

/// The status of `path`.
Status status_of(const std::string& path) {
    ZEST_CONTEXT("{}", path);
    auto found = status(path);
    ZASSERT(found.has_value());
    return *found;
}

ZEST_SUITE(fs_file_copy) {

ZEST_CASE(copy_file_copies_the_bytes) {
    // Into a new file and over one there, which on macOS takes no clone but
    // the loop of reads and writes, past what one read of it takes.
    test::TempDir dir;
    auto text = test::text_of(3 * 1024 * 1024 + 7);
    test::write_text(dir.file("é中😀.txt"), text);
    test::write_text(dir.file("there.txt"), "old");
    ZEXPECT(!copy_file(dir.file("é中😀.txt"), dir.file("new.txt")));
    ZEXPECT(!copy_file(dir.file("é中😀.txt"), dir.file("there.txt")));
    ZEXPECT(test::read_text(dir.file("new.txt")) == text);
    ZEXPECT(test::read_text(dir.file("there.txt")) == text);
    ZEXPECT(test::read_text(dir.file("é中😀.txt")) == text);
}

ZEST_CASE(copy_file_copies_an_empty_file) {
    test::TempDir dir;
    test::write_text(dir.file("empty"), "");
    ZEXPECT(!copy_file(dir.file("empty"), dir.file("copy")));
    ZEXPECT(is_file(dir.file("copy")));
    ZEXPECT(test::read_text(dir.file("copy")).empty());
}

ZEST_CASE(copy_file_replaces_what_was_there) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "new");
    test::write_text(dir.file("to"), "older and longer");
    ZEXPECT(!copy_file(dir.file("from"), dir.file("to")));
    ZEXPECT(test::read_text(dir.file("to")) == "new");
}

ZEST_CASE(copy_file_takes_the_mtime) {
    // Into a new file, which a macOS clone makes, and over one there.
    test::TempDir dir;
    test::write_text(dir.file("from"), "text");
    test::write_text(dir.file("there"), "old");
    ZASSERT(!set_times(dir.file("from"), {.mtime = past}));
    ZEXPECT(!copy_file(dir.file("from"), dir.file("new")));
    ZEXPECT(!copy_file(dir.file("from"), dir.file("there")));
    ZEXPECT(status_of(dir.file("new")).mtime == past);
    ZEXPECT(status_of(dir.file("there")).mtime == past);
}

#ifdef _WIN32
ZEST_CASE(copy_file_takes_the_attributes) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "text");
    test::write_text(dir.file("there"), "old");
    ZASSERT(::SetFileAttributesW(test::std_path(dir.file("from")).wstring().c_str(),
                                 FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN));
    ZEXPECT(!copy_file(dir.file("from"), dir.file("new")));
    ZEXPECT(!copy_file(dir.file("from"), dir.file("there")));
    // Each read-only file goes before a check could end the case: TempDir,
    // through std::filesystem, would leave it.
    for(auto name: {"new", "there"}) {
        ZEST_CONTEXT("{}", name);
        auto attributes = ::GetFileAttributesW(test::std_path(dir.file(name)).wstring().c_str());
        auto found = status(dir.file(name));
        ZEXPECT(remove(dir.file(name)) == true);
        ZEXPECT(attributes != INVALID_FILE_ATTRIBUTES);
        ZEXPECT((attributes & FILE_ATTRIBUTE_READONLY) != 0);
        ZEXPECT((attributes & FILE_ATTRIBUTE_HIDDEN) != 0);
        ZEXPECT(found.transform([](const Status& status) { return status.permissions; }) == 0444u);
    }
    ZEXPECT(remove(dir.file("from")) == true);
}

ZEST_CASE(copy_file_over_a_hidden_file_fails) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "new");
    test::write_text(dir.file("to"), "old");
    ZASSERT(::SetFileAttributesW(test::std_path(dir.file("to")).wstring().c_str(),
                                 FILE_ATTRIBUTE_HIDDEN));
    ZEXPECT(copy_file(dir.file("from"), dir.file("to")) == std::errc::permission_denied);
    ZEXPECT(test::read_text(dir.file("to")) == "old");
}
#else
ZEST_CASE(copy_file_takes_the_permissions) {
    // Exactly, whatever the umask takes from a new file's, and over a file
    // there, but for setuid and setgid.
    test::TempDir dir;
    test::write_text(dir.file("from"), "text");
    test::write_text(dir.file("there"), "old");
    ZASSERT(!set_permissions(dir.file("there"), 0666));
    ZASSERT(!set_permissions(dir.file("from"), 06757));
    ZEXPECT(!copy_file(dir.file("from"), dir.file("new")));
    ZEXPECT(!copy_file(dir.file("from"), dir.file("there")));
    ZEXPECT(status_of(dir.file("new")).permissions == 0757u);
    ZEXPECT(status_of(dir.file("there")).permissions == 0757u);
}

ZEST_CASE(copy_file_copies_a_file_it_may_not_write) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "text");
    ZASSERT(!set_permissions(dir.file("from"), 0444));
    ZEXPECT(!copy_file(dir.file("from"), dir.file("to")));
    ZEXPECT(test::read_text(dir.file("to")) == "text");
    ZEXPECT(status_of(dir.file("to")).permissions == 0444u);
}
#endif

ZEST_CASE(copy_file_over_a_read_only_file_fails) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "new");
    test::write_text(dir.file("to"), "old");
    ZASSERT(!set_permissions(dir.file("to"), 0444));
#ifndef _WIN32
    if(::access(dir.file("to").c_str(), W_OK) == 0) {
        // Root writes any file.
        zest::skip();
        return;
    }
#endif
    ZEXPECT(copy_file(dir.file("from"), dir.file("to")) == std::errc::permission_denied);
    ZEXPECT(test::read_text(dir.file("to")) == "old");
    ZEXPECT(!set_permissions(dir.file("to"), 0644));
}

ZEST_CASE(copy_file_follows_a_symlink) {
    test::TempDir dir;
    test::write_text(dir.file("target"), "text");
    if(!test::linked(create_symlink("target", dir.file("link")))) {
        return;
    }
    ZEXPECT(!copy_file(dir.file("link"), dir.file("copy")));
    ZEXPECT(status_of(dir.file("copy")).type == FileType::Regular);
    ZEXPECT(test::read_text(dir.file("copy")) == "text");
}

ZEST_CASE(copy_file_writes_through_a_symlink) {
    test::TempDir dir;
    test::write_text(dir.file("from"), "new");
    test::write_text(dir.file("target"), "old");
    if(!test::linked(create_symlink("target", dir.file("link")))) {
        return;
    }
    ZEXPECT(!copy_file(dir.file("from"), dir.file("link")));
    ZEXPECT(is_symlink(dir.file("link")));
    ZEXPECT(test::read_text(dir.file("target")) == "new");
}

ZEST_CASE(copy_file_onto_itself_fails) {
    // By its name or another, a hard link: emptying it would lose it. Read
    // only, it fails the same, though no one may write it.
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    ZASSERT(!create_hard_link(dir.file("file"), dir.file("other")));
    ZEXPECT(copy_file(dir.file("file"), dir.file("file")) == std::errc::invalid_argument);
    ZEXPECT(copy_file(dir.file("file"), dir.file("other")) == std::errc::invalid_argument);
    ZASSERT(!set_permissions(dir.file("file"), 0444));
    ZEXPECT(copy_file(dir.file("file"), dir.file("other")) == std::errc::invalid_argument);
    ZEXPECT(!set_permissions(dir.file("file"), 0644));
    ZEXPECT(test::read_text(dir.file("file")) == "text");
}

ZEST_CASE(copy_file_of_a_directory_fails) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "sub");
    ZEXPECT(copy_file(dir.file("sub"), dir.file("copy")) == std::errc::invalid_argument);
    ZEXPECT(exists(dir.file("copy")) == false);
}

#ifndef _WIN32
ZEST_CASE(copy_file_of_a_fifo_fails) {
    // Without waiting for a writer.
    test::TempDir dir;
    ZASSERT(::mkfifo(dir.file("fifo").c_str(), 0644) == 0);
    ZEXPECT(copy_file(dir.file("fifo"), dir.file("copy")) == std::errc::invalid_argument);
    ZEXPECT(exists(dir.file("copy")) == false);
}

ZEST_CASE(copy_file_onto_a_fifo_fails) {
    // Without waiting for a reader.
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    ZASSERT(::mkfifo(dir.file("fifo").c_str(), 0644) == 0);
    ZEXPECT(copy_file(dir.file("file"), dir.file("fifo")) == std::errc::invalid_argument);
    ZEXPECT(status_of(dir.file("fifo")).type == FileType::Fifo);
}

ZEST_CASE(copy_file_onto_a_device_fails) {
    // Whose permissions are not the copy's to change, root's or not.
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto before = status_of("/dev/null");
    ZEXPECT(copy_file(dir.file("file"), "/dev/null") == std::errc::invalid_argument);
    ZEXPECT(status_of("/dev/null").permissions == before.permissions);
}
#endif

ZEST_CASE(copy_file_onto_a_directory_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    std::filesystem::create_directory(dir.path / "sub");
    ZEXPECT(copy_file(dir.file("file"), dir.file("sub")) == std::errc::is_a_directory);
    ZEXPECT(is_directory(dir.file("sub")));
}

ZEST_CASE(copy_file_of_nothing_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    ZEXPECT(copy_file(dir.file("missing"), dir.file("copy")) ==
            std::errc::no_such_file_or_directory);
    ZEXPECT(copy_file(dir.file("file"), dir.file("missing/copy")) ==
            std::errc::no_such_file_or_directory);
    ZEXPECT(exists(dir.file("copy")) == false);
}

#ifdef __linux__
ZEST_CASE(copy_file_copies_across_file_systems) {
    // Where copy_file_range fails since Linux 5.19, and sendfile copies.
    if(!test::across_file_systems()) {
        return;
    }
    test::TempDir dir;
    test::TempDir other("/dev/shm");
    auto text = test::text_of(1024 * 1024 + 7);
    test::write_text(dir.file("from"), text);
    ZASSERT(!set_permissions(dir.file("from"), 0640));
    ZASSERT(!set_times(dir.file("from"), {.mtime = past}));
    ZEXPECT(!copy_file(dir.file("from"), other.file("to")));
    ZEXPECT(test::read_text(other.file("to")) == text);
    auto copied = status_of(other.file("to"));
    ZEXPECT(copied.permissions == 0640u);
    ZEXPECT(copied.mtime == past);
}

ZEST_CASE(copy_file_copies_a_file_that_reports_no_size) {
    // A file under /proc, whose end copy_file_range and sendfile take for
    // its start.
    test::TempDir dir;
    ZEXPECT(!copy_file("/proc/self/cmdline", dir.file("copy")));
    auto copied = test::read_text(dir.file("copy"));
    ZEXPECT(!copied.empty());
    ZEXPECT(copied == test::read_text("/proc/self/cmdline"));
}
#endif

};  // ZEST_SUITE(fs_file_copy)

}  // namespace

}  // namespace kota::fs
