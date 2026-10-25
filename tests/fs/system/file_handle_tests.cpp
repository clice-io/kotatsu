#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/fs/directory.h"
#include "kota/fs/file.h"
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

ZEST_SUITE(fs_file_handle) {

ZEST_CASE(open_with_options_refused_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    for(auto options: {
            OpenOptions{},
            OpenOptions{.truncate = true},
            OpenOptions{.read = true, .create = true},
            OpenOptions{.read = true, .create_new = true},
            OpenOptions{.append = true, .truncate = true}
    }) {
        auto file = File::open(dir.file("file"), options);
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

ZEST_CASE(open_to_write_an_existing_file_without_create_or_truncate) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "old");
    auto file = File::open(dir.file("file"), {.write = true});
    ZASSERT(file.has_value());
    ZEXPECT(!file->write("n"));
    ZEXPECT(!file->close());
    ZEXPECT(test::read_text(dir.file("file")) == "nld");
    auto missing = File::open(dir.file("missing"), {.write = true});
    ZASSERT(!missing.has_value());
    ZEXPECT(missing.error() == std::errc::no_such_file_or_directory);
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

ZEST_CASE(open_to_truncate_empties_an_existing_file) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "old");
    auto file = File::open(dir.file("file"), {.read = true, .write = true, .truncate = true});
    ZASSERT(file.has_value());
    ZEXPECT(rest_of(*file) == "");
    auto missing = File::open(dir.file("missing"), {.write = true, .truncate = true});
    ZASSERT(!missing.has_value());
    ZEXPECT(missing.error() == std::errc::no_such_file_or_directory);
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

ZEST_CASE(status_of_a_file_open_to_write) {
    test::TempDir dir;
    auto file = File::open(dir.file("file"), {.write = true, .create = true});
    ZASSERT(file.has_value());
    ZEXPECT(!file->write("text"));
    auto found = file->status();
    ZASSERT(found.has_value());
    ZEXPECT(found->size == 4u);
}

ZEST_CASE(status_of_a_closed_file_fails) {
    File file;
    auto found = file.status();
    ZASSERT(!found.has_value());
    ZEXPECT(found.error() == std::errc::bad_file_descriptor);
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

};  // ZEST_SUITE(fs_file_handle)

}  // namespace

}  // namespace kota::fs
