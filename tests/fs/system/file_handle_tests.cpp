#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "fs/harness/files.h"
#include "fs/harness/win32.h"
#include "kota/zest/zest.h"
#include "kota/fs/directory.h"
#include "kota/fs/file.h"
#include "kota/fs/path.h"
#include "kota/fs/resolve.h"
#include "kota/fs/status.h"

namespace kota::fs {

namespace {

/// What `file` reads from where it stands to the end.
std::string rest_of(File& file) {
    std::string content;
    std::array<char, 64> buffer{};
    while(true) {
        auto count = file.read(buffer);
        ZASSERT(count.has_value());
        if(*count == 0) {
            return content;
        }
        content.append(buffer.data(), *count);
    }
}

/// Opens the directory at `path` each kind of way OpenOptions takes, all of
/// which fail: a File opens no directory.
void expect_refused_as_a_directory(std::string_view path) {
    struct Refused {
        std::string_view label;
        OpenOptions options;
        std::errc error;
    };

    constexpr auto is_a_directory = std::errc::is_a_directory;
    constexpr Refused refused[] = {
        {"read",                    {.read = true},                                  is_a_directory        },
        {"write",                   {.write = true},                                 is_a_directory        },
        {"read, write",             {.read = true, .write = true},                   is_a_directory        },
        {"append",                  {.append = true},                                is_a_directory        },
        {"read, append",            {.read = true, .append = true},                  is_a_directory        },
        {"write, truncate",         {.write = true, .truncate = true},               is_a_directory        },
        {"read, write, truncate",   {.read = true, .write = true, .truncate = true}, is_a_directory        },
        {"write, create",           {.write = true, .create = true},                 is_a_directory        },
        {"read, write, create",     {.read = true, .write = true, .create = true},   is_a_directory        },
        {"write, truncate, create",
         {.write = true, .truncate = true, .create = true},
         is_a_directory                                                                                    },
        {"append, create",          {.append = true, .create = true},                is_a_directory        },
        // Anything there, a directory too.
        {"write, create_new",       {.write = true, .create_new = true},             std::errc::file_exists},
        {"read, write, create_new",
         {.read = true, .write = true, .create_new = true},
         std::errc::file_exists                                                                            },
        {"append, create_new",      {.append = true, .create_new = true},            std::errc::file_exists},
    };
    for(const auto& entry: refused) {
        ZEST_CONTEXT("{}", entry.label);
        auto file = File::open(path, entry.options);
        ZASSERT(!file.has_value());
        ZEXPECT(file.error() == entry.error);
    }
}

#ifdef __APPLE__
/// A disk image of 1 MB of HFS+ in a test's directory, attached beside it,
/// which hdiutil lets any user do; detached, as though the disk went, when
/// this goes or a failed ZASSERT ends the process.
struct DiskImage {
    explicit DiskImage(const test::TempDir& dir) :
        image(dir.file("image.dmg")), mount(dir.file("volume")) {
        std::filesystem::create_directory(test::std_path(mount));
        ZASSERT(hdiutil(std::format("create -size 1m -fs HFS+ -volname kota '{}'", image)));
        attach(false);
    }

    DiskImage(const DiskImage&) = delete;
    DiskImage& operator=(const DiskImage&) = delete;

    ~DiskImage() {
        detach();
    }

    /// `name` on the volume.
    std::string file(std::string_view name) const {
        return std::format("{}/{}", mount, name);
    }

    void attach(bool read_only) {
        attached =
            hdiutil(std::format("attach {} -nobrowse -noverify -noautofsck -mountpoint '{}' '{}'",
                                read_only ? "-readonly" : "",
                                mount,
                                image));
        ZASSERT(attached);
    }

    /// Detaches it, files open on it or not: false when it stays attached.
    bool detach() {
        if(attached && hdiutil(std::format("detach -force '{}'", mount))) {
            attached = false;
        }
        return !attached;
    }

private:
    static bool hdiutil(std::string_view arguments) {
        return std::system(std::format("hdiutil {} > /dev/null", arguments).c_str()) == 0;
    }

    const std::string image;
    const std::string mount;
    bool attached = false;
    zest::FatalHook on_fatal{[this] { detach(); }};
};
#endif

ZEST_SUITE(fs_file_handle) {

ZEST_CASE(open_reads_and_writes_through_the_handle) {
    test::TempDir dir;
    auto file = File::open(dir.file("file"), {.write = true, .create = true});
    ZASSERT(file.has_value());
    ZEXPECT(file->is_open());
    ZEXPECT(!file->write("hello"));
    ZEXPECT(!file->close());
    ZEXPECT(!file->is_open());
    ZEXPECT(test::read_text(dir.file("file")) == "hello");

    auto reader = File::open(dir.file("file"));
    ZASSERT(reader.has_value());
    std::array<char, 16> buffer{};
    auto count = reader->read(buffer);
    ZASSERT(count.has_value());
    ZEXPECT(std::string(buffer.data(), *count) == "hello");
    ZEXPECT(reader->read(buffer) == 0u);
}

ZEST_CASE(open_with_options_refused_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");

    struct Refused {
        std::string_view label;
        OpenOptions options;
    };

    constexpr Refused refused[] = {
        {"nothing",          {}                                },
        {"truncate",         {.truncate = true}                },
        {"read, create",     {.read = true, .create = true}    },
        {"read, create_new", {.read = true, .create_new = true}},
        {"read, truncate",   {.read = true, .truncate = true}  },
        {"append, truncate", {.append = true, .truncate = true}},
    };
    for(const auto& entry: refused) {
        ZEST_CONTEXT("{}", entry.label);
        auto file = File::open(dir.file("file"), entry.options);
        ZASSERT(!file.has_value());
        ZEXPECT(file.error() == std::errc::invalid_argument);
    }
    ZEXPECT(test::read_text(dir.file("file")) == "text");
}

ZEST_CASE(open_to_read_and_write_keeps_the_bytes) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "hello");
    auto file = File::open(dir.file("file"), {.read = true, .write = true});
    ZASSERT(file.has_value());
    ZEXPECT(rest_of(*file) == "hello");
    ZEXPECT(file->seek(0) == 0u);
    ZEXPECT(!file->write("J"));
    ZEXPECT(!file->close());
    ZEXPECT(test::read_text(dir.file("file")) == "Jello");
}

ZEST_CASE(open_to_write_writes_over_the_start) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "old");
    auto file = File::open(dir.file("file"), {.write = true});
    ZASSERT(file.has_value());
    ZEXPECT(!file->write("n"));
    ZEXPECT(!file->close());
    ZEXPECT(test::read_text(dir.file("file")) == "nld");
}

ZEST_CASE(open_to_write_of_nothing_fails) {
    test::TempDir dir;
    auto file = File::open(dir.file("missing"), {.write = true});
    ZASSERT(!file.has_value());
    ZEXPECT(file.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(open_to_create_keeps_an_existing_file) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "old");
    auto file = File::open(dir.file("file"), {.write = true, .create = true});
    ZASSERT(file.has_value());
    ZEXPECT(!file->write("n"));
    ZEXPECT(!file->close());
    ZEXPECT(test::read_text(dir.file("file")) == "nld");
    ZEXPECT(File::open(dir.file("new"), {.write = true, .create = true}).has_value());
    ZEXPECT(is_file(dir.file("new")));
}

ZEST_CASE(open_to_create_new_over_anything_fails) {
    test::TempDir dir;
    ZEXPECT(File::open(dir.file("new"), {.write = true, .create_new = true}).has_value());
    auto again = File::open(dir.file("new"), {.write = true, .create_new = true});
    ZASSERT(!again.has_value());
    ZEXPECT(again.error() == std::errc::file_exists);
}

ZEST_CASE(open_to_create_new_over_a_dangling_link_fails) {
    test::TempDir dir;
    if(!test::linked(create_symlink("missing", dir.file("link")))) {
        return;
    }
    auto file = File::open(dir.file("link"), {.write = true, .create_new = true});
    ZASSERT(!file.has_value());
    ZEXPECT(file.error() == std::errc::file_exists);
    ZEXPECT(!is_file(dir.file("missing")));
}

ZEST_CASE(open_to_create_new_ignores_create_and_truncate) {
    test::TempDir dir;
    test::write_text(dir.file("taken"), "old");

    struct Accepted {
        std::string_view label;
        OpenOptions options;
    };

    constexpr Accepted accepted[] = {
        {"write, truncate, create_new",  {.write = true, .truncate = true, .create_new = true} },
        {"append, truncate, create_new", {.append = true, .truncate = true, .create_new = true}},
        {"write, create, create_new",    {.write = true, .create = true, .create_new = true}   },
    };
    for(std::size_t i = 0; i < std::size(accepted); ++i) {
        ZEST_CONTEXT("{}", accepted[i].label);
        auto name = dir.file(std::to_string(i));
        auto file = File::open(name, accepted[i].options);
        ZASSERT(file.has_value());
        ZEXPECT(!file->write("new"));
        ZEXPECT(!file->close());
        ZEXPECT(test::read_text(name) == "new");
        auto taken = File::open(dir.file("taken"), accepted[i].options);
        ZASSERT(!taken.has_value());
        ZEXPECT(taken.error() == std::errc::file_exists);
    }
    ZEXPECT(test::read_text(dir.file("taken")) == "old");
}

ZEST_CASE(open_to_truncate_empties_the_file) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "old");
    auto file = File::open(dir.file("file"), {.read = true, .write = true, .truncate = true});
    ZASSERT(file.has_value());
    ZEXPECT(rest_of(*file) == "");
}

ZEST_CASE(open_to_truncate_nothing_fails) {
    test::TempDir dir;
    auto file = File::open(dir.file("missing"), {.write = true, .truncate = true});
    ZASSERT(!file.has_value());
    ZEXPECT(file.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(open_to_append_writes_at_the_end) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "a");
    auto first = File::open(dir.file("file"), {.append = true});
    ZASSERT(first.has_value());
    auto second = File::open(dir.file("file"), {.read = true, .append = true});
    ZASSERT(second.has_value());
    ZEXPECT(!first->write("b"));
    // Wherever the other handle wrote, and wherever this one stands.
    ZEXPECT(!second->write("c"));
    ZEXPECT(second->seek(0) == 0u);
    // A read starts where the seek left it.
    std::array<char, 1> first_byte{};
    ZEXPECT(second->read(first_byte) == 1u);
    ZEXPECT(first_byte[0] == 'a');
    ZEXPECT(!second->write("d"));
    ZEXPECT(!first->write("e"));
    ZEXPECT(test::read_text(dir.file("file")) == "abcde");
}

ZEST_CASE(open_to_append_creates) {
    test::TempDir dir;
    auto file = File::open(dir.file("file"), {.append = true, .create = true});
    ZASSERT(file.has_value());
    ZEXPECT(!file->write("text"));
    ZEXPECT(!file->close());
    ZEXPECT(test::read_text(dir.file("file")) == "text");
}

ZEST_CASE(open_of_a_directory_fails) {
    test::TempDir dir;
    expect_refused_as_a_directory(dir.root());
}

ZEST_CASE(open_of_a_directory_through_a_link_fails) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
    test::link_directory(dir.file("target"), dir.file("link"));
    expect_refused_as_a_directory(dir.file("link"));
}

#ifndef _WIN32
ZEST_CASE(open_of_a_directory_that_may_not_be_read_fails) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "closed");
    test::ScopedPermissions closed(dir.path / "closed", std::filesystem::perms::none);
    if(::access(dir.file("closed").c_str(), R_OK) == 0) {
        // Root passes through any permission.
        zest::skip();
        return;
    }
    expect_refused_as_a_directory(dir.file("closed"));
}
#endif

ZEST_CASE(open_to_write_a_read_only_file_fails) {
    // Refused as a directory is on Windows, and still a file.
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    test::ScopedPermissions read_only(dir.path / "file", std::filesystem::perms::owner_read);
#ifndef _WIN32
    if(::access(dir.file("file").c_str(), W_OK) == 0) {
        // Root passes through any permission.
        zest::skip();
        return;
    }
#endif
    auto file = File::open(dir.file("file"), {.write = true});
    ZASSERT(!file.has_value());
    ZEXPECT(file.error() == std::errc::permission_denied);
}

#ifdef _WIN32
ZEST_CASE(open_to_create_new_over_a_dangling_junction_fails) {
    // Refused as a directory is.
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "gone");
    test::make_junction(dir.file("dangling"), dir.file("gone"));
    std::filesystem::remove(dir.path / "gone");
    auto file = File::open(dir.file("dangling"), {.write = true, .create_new = true});
    ZASSERT(!file.has_value());
    ZEXPECT(file.error() == std::errc::file_exists);
}
#endif

ZEST_CASE(seek_moves_where_reads_start) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "0123456789");
    auto file = File::open(dir.file("file"));
    ZASSERT(file.has_value());
    ZEXPECT(file->seek(4) == 4u);
    std::array<char, 2> buffer{};
    ZEXPECT(file->read(buffer) == 2u);
    ZEXPECT(std::string_view(buffer.data(), 2) == "45");
    ZEXPECT(file->seek(-1, SeekFrom::End) == 9u);
    ZEXPECT(rest_of(*file) == "9");
    ZEXPECT(file->seek(-3, SeekFrom::Current) == 7u);
    ZEXPECT(rest_of(*file) == "789");
}

ZEST_CASE(seek_past_the_end_leaves_a_hole) {
    test::TempDir dir;
    auto file = File::open(dir.file("file"), {.read = true, .write = true, .create = true});
    ZASSERT(file.has_value());
    ZEXPECT(file->seek(3) == 3u);
    ZEXPECT(!file->write("x"));
    ZEXPECT(!file->close());
    ZEXPECT(test::read_text(dir.file("file")) == std::string("\0\0\0x", 4));
}

ZEST_CASE(seek_before_the_start_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto file = File::open(dir.file("file"));
    ZASSERT(file.has_value());
    auto moved = file->seek(-1);
    ZASSERT(!moved.has_value());
    ZEXPECT(moved.error() == std::errc::invalid_argument);
}

ZEST_CASE(status_of_an_open_file_follows_it) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto file = File::open(dir.file("file"), {.read = true, .write = true});
    ZASSERT(file.has_value());
    auto opened = file->status();
    ZASSERT(opened.has_value());
    auto named = status(dir.file("file"));
    ZASSERT(named.has_value());
    ZEXPECT(opened->type == FileType::Regular);
    ZEXPECT(opened->size == 4u);
    ZEXPECT(opened->id == named->id);
    ZEXPECT(opened->mtime == named->mtime);
    // Renamed, the open file is the same one.
    ZEXPECT(!rename(dir.file("file"), dir.file("moved")));
    ZEXPECT(!file->write("longer"));
    auto moved = file->status();
    ZASSERT(moved.has_value());
    ZEXPECT(moved->id == named->id);
    ZEXPECT(moved->size == 6u);
}

ZEST_CASE(status_of_a_file_open_to_write_tells_its_size) {
    test::TempDir dir;
    auto file = File::open(dir.file("file"), {.write = true, .create = true});
    ZASSERT(file.has_value());
    ZEXPECT(!file->write("text"));
    auto found = file->status();
    ZASSERT(found.has_value());
    ZEXPECT(found->size == 4u);
}

ZEST_CASE(status_of_an_open_file_outlives_its_name) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto file = File::open(dir.file("file"));
    ZASSERT(file.has_value());
    auto named = status(dir.file("file"));
    ZASSERT(named.has_value());
    ZEXPECT(remove(dir.file("file")) == true);
    auto opened = file->status();
    ZASSERT(opened.has_value());
    ZEXPECT(opened->id == named->id);
    ZEXPECT(opened->size == 4u);
}

ZEST_CASE(status_of_a_closed_file_fails) {
    File file;
    auto found = file.status();
    ZASSERT(!found.has_value());
    ZEXPECT(found.error() == std::errc::bad_file_descriptor);
}

ZEST_CASE(a_moved_file_keeps_its_handle) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto opened = File::open(dir.file("file"));
    ZASSERT(opened.has_value());
    auto handle = opened->native_handle();
    File moved(std::move(*opened));
    ZEXPECT(!opened->is_open());
    ZEXPECT(opened->native_handle() == File::closed);
    ZEXPECT(moved.native_handle() == handle);
    File assigned;
    assigned = std::move(moved);
    ZEXPECT(!moved.is_open());
    ZEXPECT(assigned.native_handle() == handle);
    // Closing what is closed does nothing.
    ZEXPECT(!moved.close());
}

ZEST_CASE(read_after_close_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto file = File::open(dir.file("file"));
    ZASSERT(file.has_value());
    ZEXPECT(!file->close());
    std::array<char, 16> buffer{};
    auto count = file->read(buffer);
    ZASSERT(!count.has_value());
    ZEXPECT(count.error() == std::errc::bad_file_descriptor);
    ZEXPECT(file->write("more") == std::errc::bad_file_descriptor);
}

ZEST_CASE(resize_cuts_and_extends) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "0123456789");
    auto file = File::open(dir.file("file"), {.read = true, .write = true});
    ZASSERT(file.has_value());
    ZEXPECT(!file->resize(3));
    ZEXPECT(test::read_text(dir.file("file")) == "012");
    ZEXPECT(!file->resize(5));
    ZEXPECT(test::read_text(dir.file("file")) == std::string("012\0\0", 5));
    // The position stays where it was.
    ZEXPECT(rest_of(*file) == std::string("012\0\0", 5));
}

ZEST_CASE(resize_of_a_file_open_to_read_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto file = File::open(dir.file("file"));
    ZASSERT(file.has_value());
#ifdef _WIN32
    ZEXPECT(file->resize(0) == std::errc::permission_denied);
#else
    ZEXPECT(file->resize(0) == std::errc::invalid_argument);
#endif
    ZEXPECT(test::read_text(dir.file("file")) == "text");
}

ZEST_CASE(sync_keeps_what_was_written) {
    test::TempDir dir;
    auto file = File::open(dir.file("file"), {.write = true, .create = true});
    ZASSERT(file.has_value());
    ZEXPECT(!file->write("text"));
    ZEXPECT(!file->sync());
    ZEXPECT(test::read_text(dir.file("file")) == "text");
}

ZEST_CASE(sync_of_a_file_open_to_read_succeeds) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto file = File::open(dir.file("file"));
    ZASSERT(file.has_value());
    ZEXPECT(!file->sync());
}

#ifdef _WIN32
ZEST_CASE(sync_of_a_read_only_file_fails) {
    // Windows flushes a file only through a handle that may write it.
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    test::ScopedPermissions read_only(dir.path / "file", std::filesystem::perms::owner_read);
    auto file = File::open(dir.file("file"));
    ZASSERT(file.has_value());
    ZEXPECT(file->sync() == std::errc::permission_denied);
}
#endif

#ifdef __APPLE__
// macOS refuses F_FULLFSYNC for a device, a read-only volume and a pipe,
// where fsync() stands in, and for a file whose volume went, where fsync()
// would succeed. Its other errors, such as a flush that failed, take a
// failing disk to show.

ZEST_CASE(sync_of_a_device_succeeds) {
    auto file = File::open("/dev/null", {.write = true});
    ZASSERT(file.has_value());
    ZEXPECT(!file->sync());
}

ZEST_CASE(sync_of_a_file_on_a_read_only_volume_succeeds) {
    test::TempDir dir;
    DiskImage image(dir);
    test::write_text(image.file("file"), "text");
    ZASSERT(image.detach());
    image.attach(true);
    auto file = File::open(image.file("file"));
    ZASSERT(file.has_value());
    ZEXPECT(!file->sync());
}

ZEST_CASE(sync_of_a_file_whose_volume_went_fails) {
    test::TempDir dir;
    DiskImage image(dir);
    auto file = File::open(image.file("file"), {.write = true, .create = true});
    ZASSERT(file.has_value());
    ZEXPECT(!file->write("text"));
    ZASSERT(image.detach());
    ZEXPECT(file->sync() == std::errc::bad_file_descriptor);
}

ZEST_CASE(sync_of_a_pipe_fails) {
    int ends[2];
    ZASSERT(::pipe(ends) == 0);
    auto file = File::open(std::format("/dev/fd/{}", ends[1]), {.write = true});
    ZASSERT(file.has_value());
    ZEXPECT(file->sync() == std::errc::not_supported);
    ::close(ends[0]);
    ::close(ends[1]);
}
#endif

ZEST_CASE(set_sparse_lets_a_file_hold_a_hole) {
    test::TempDir dir;
    auto file = File::open(dir.file("file"), {.write = true, .create = true});
    ZASSERT(file.has_value());
    ZEXPECT(!file->set_sparse());
    constexpr std::uint64_t size = std::uint64_t{1} << 30;
    ZEXPECT(!file->resize(size));
    auto found = file->status();
    ZASSERT(found.has_value());
    ZEXPECT(found->size == size);
}

ZEST_CASE(canonical_names_the_open_file_as_canonical_does) {
    test::TempDir dir;
    test::write_text(dir.file("é中😀.txt"), "text");
    auto file = File::open(dir.file("é中😀.txt"));
    ZASSERT(file.has_value());
    std::string opened;
    ZEXPECT(!file->canonical(opened));
    std::string named;
    ZEXPECT(!fs::canonical(named, dir.file("é中😀.txt")));
    ZEXPECT(opened == named);
}

ZEST_CASE(canonical_follows_a_link_the_file_was_opened_through) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "target");
    test::write_text(dir.file("target/file.txt"), "text");
    test::link_directory(dir.file("target"), dir.file("link"));
    auto file = File::open(dir.file("link/file.txt"));
    ZASSERT(file.has_value());
    std::string opened;
    ZEXPECT(!file->canonical(opened));
    std::string target;
    ZEXPECT(!fs::canonical(target, dir.file("target/file.txt")));
    ZEXPECT(opened == target);
}

ZEST_CASE(canonical_of_a_renamed_file_gives_its_new_name) {
    test::TempDir dir;
    test::write_text(dir.file("old.txt"), "text");
    auto file = File::open(dir.file("old.txt"));
    ZASSERT(file.has_value());
    ZEXPECT(!rename(dir.file("old.txt"), dir.file("new.txt")));
    std::string opened;
    ZEXPECT(!file->canonical(opened));
    std::string named;
    ZEXPECT(!fs::canonical(named, dir.file("new.txt")));
    ZEXPECT(opened == named);
}

ZEST_CASE(canonical_of_a_removed_file_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    auto file = File::open(dir.file("file.txt"));
    ZASSERT(file.has_value());
    ZEXPECT(remove(dir.file("file.txt")) == true);
    std::string opened;
    ZEXPECT(file->canonical(opened) == std::errc::no_such_file_or_directory);
}

ZEST_CASE(canonical_of_a_file_whose_name_went_gives_another_or_fails) {
    // Another hard link still names the file, which the system may give.
    test::TempDir dir;
    test::write_text(dir.file("a.txt"), "text");
    ZEXPECT(!create_hard_link(dir.file("a.txt"), dir.file("b.txt")));
    auto file = File::open(dir.file("a.txt"));
    ZASSERT(file.has_value());
    ZEXPECT(remove(dir.file("a.txt")) == true);
    std::string opened;
    auto error = file->canonical(opened);
    std::string other;
    ZASSERT(!fs::canonical(other, dir.file("b.txt")));
    ZEST_CONTEXT("{}", opened);
    if(error) {
        ZEXPECT(error == std::errc::no_such_file_or_directory);
    } else {
        ZEXPECT(opened == other);
    }
#ifdef __linux__
    // Linux keeps the name that went, marked " (deleted)".
    ZEXPECT(error == std::errc::no_such_file_or_directory);
#endif
}

#ifdef _WIN32
ZEST_CASE(canonical_spells_the_open_file_as_on_disk) {
    test::TempDir dir;
    test::write_text(dir.file("MixedCase.txt"), "text");
    auto file = File::open(dir.file("mixedcase.TXT"));
    ZASSERT(file.has_value());
    std::string opened;
    ZEXPECT(!file->canonical(opened));
    ZEXPECT(path::filename(opened) == "MixedCase.txt");
}
#endif

#ifdef __linux__
ZEST_CASE(canonical_of_a_file_named_as_deleted_gives_its_name) {
    // /proc marks a name that went with " (deleted)"; one that still leads
    // to the file is the file's own.
    test::TempDir dir;
    test::write_text(dir.file("file (deleted)"), "text");
    auto file = File::open(dir.file("file (deleted)"));
    ZASSERT(file.has_value());
    std::string opened;
    ZEXPECT(!file->canonical(opened));
    std::string named;
    ZEXPECT(!fs::canonical(named, dir.file("file (deleted)")));
    ZEXPECT(opened == named);
}

ZEST_CASE(canonical_of_a_removed_file_whose_marked_name_another_took_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto file = File::open(dir.file("file"));
    ZASSERT(file.has_value());
    ZEXPECT(remove(dir.file("file")) == true);
    test::write_text(dir.file("file (deleted)"), "other");
    std::string opened;
    ZEXPECT(file->canonical(opened) == std::errc::no_such_file_or_directory);
}
#endif

ZEST_CASE(canonical_of_a_pipe_fails) {
    // No path names a pipe, though Windows names it to open it.
#ifdef _WIN32
    test::TempDir dir;
    std::string name;
    HANDLE reader = test::make_pipe(name, dir, PIPE_ACCESS_INBOUND, 4096);
    auto file = File::open(name, {.write = true});
    std::string opened;
    auto error = file ? file->canonical(opened) : file.error();
    ::CloseHandle(reader);
#else
    int ends[2];
    ZASSERT(::pipe(ends) == 0);
    auto file = File::open(std::format("/dev/fd/{}", ends[0]));
    std::string opened;
    auto error = file ? file->canonical(opened) : file.error();
    ::close(ends[0]);
    ::close(ends[1]);
#endif
    ZEST_CONTEXT("{}", opened);
    ZASSERT(file.has_value());
    ZEXPECT(error == std::errc::no_such_file_or_directory);
}

#ifdef _WIN32
ZEST_CASE(canonical_of_a_device_fails) {
    // Windows names no device by a path, as Linux names /dev/null.
    auto file = File::open("nul");
    ZASSERT(file.has_value());
    std::string opened;
    ZEXPECT(file->canonical(opened) == std::errc::no_such_file_or_directory);
}
#endif

ZEST_CASE(canonical_of_a_closed_file_fails) {
    File file;
    std::string opened;
    ZEXPECT(file.canonical(opened) == std::errc::bad_file_descriptor);
}

ZEST_CASE(into_descriptor_hands_the_file_to_the_c_runtime) {
    test::TempDir dir;
    auto file = File::open(dir.file("file"), {.write = true, .create = true});
    ZASSERT(file.has_value());
    ZEXPECT(!file->write("hello "));
    auto fd = std::move(*file).into_descriptor();
    ZASSERT(fd.has_value());
    ZEXPECT(!file->is_open());
    // Where the File left off, through the C runtime's own calls.
#ifdef _WIN32
    ZEXPECT(::_write(*fd, "world", 5) == 5);
    ZEXPECT(::_close(*fd) == 0);
#else
    ZEXPECT(::write(*fd, "world", 5) == 5);
    ZEXPECT(::close(*fd) == 0);
#endif
    ZEXPECT(test::read_text(dir.file("file")) == "hello world");
}

ZEST_CASE(into_descriptor_writes_bytes_as_they_are) {
    // Binary mode: a C runtime in text mode would write "\r\n".
    test::TempDir dir;
    auto file = File::open(dir.file("file"), {.write = true, .create = true});
    ZASSERT(file.has_value());
    auto fd = std::move(*file).into_descriptor();
    ZASSERT(fd.has_value());
#ifdef _WIN32
    ZEXPECT(::_write(*fd, "a\nb", 3) == 3);
    ZEXPECT(::_close(*fd) == 0);
#else
    ZEXPECT(::write(*fd, "a\nb", 3) == 3);
    ZEXPECT(::close(*fd) == 0);
#endif
    ZEXPECT(test::read_text(dir.file("file")) == "a\nb");
}

ZEST_CASE(into_descriptor_keeps_the_lock) {
    test::TempDir dir;
    test::write_text(dir.file("lock"), "text");
    auto first = File::open(dir.file("lock"), {.read = true, .write = true});
    auto second = File::open(dir.file("lock"), {.read = true, .write = true});
    ZASSERT(first.has_value());
    ZASSERT(second.has_value());
    ZEXPECT(!first->lock());
    auto fd = std::move(*first).into_descriptor();
    ZASSERT(fd.has_value());
    ZEXPECT(second->try_lock() == false);
#ifdef _WIN32
    // Windows releases a lock left on a closed handle in its own time.
    ZEXPECT(::_close(*fd) == 0);
#else
    ZEXPECT(::close(*fd) == 0);
    ZEXPECT(second->try_lock() == true);
#endif
}

ZEST_CASE(into_descriptor_of_a_closed_file_fails) {
    File file;
    auto fd = std::move(file).into_descriptor();
    ZASSERT(!fd.has_value());
    ZEXPECT(fd.error() == std::errc::bad_file_descriptor);
}

};  // ZEST_SUITE(fs_file_handle)

}  // namespace

}  // namespace kota::fs
