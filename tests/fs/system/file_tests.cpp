#include <array>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <system_error>

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/fs/directory.h"
#include "kota/fs/file.h"
#include "kota/fs/path.h"

namespace kota::fs {

namespace {

using namespace std::literals;

ZEST_SUITE(fs_file) {

ZEST_CASE(write_file_and_read_file_roundtrip) {
    test::TempDir dir;
    ZEXPECT(!write_file(dir.file("é中😀.txt"), "text\0with a NUL"sv));
    ZEXPECT(read_file(dir.file("é中😀.txt")) == std::string("text\0with a NUL"sv));
    // Writing again empties the file first.
    ZEXPECT(!write_file(dir.file("é中😀.txt"), "x"));
    ZEXPECT(read_file(dir.file("é中😀.txt")) == std::string("x"));
}

ZEST_CASE(read_file_reads_past_its_buffer) {
    test::TempDir dir;
    std::string large(100'000, '\0');
    for(std::size_t i = 0; i < large.size(); ++i) {
        large[i] = static_cast<char>('a' + i % 26);
    }
    test::write_text(dir.file("large"), large);
    ZEXPECT(read_file(dir.file("large")) == large);
    test::write_text(dir.file("empty"), "");
    ZEXPECT(read_file(dir.file("empty")) == std::string());
}

ZEST_CASE(read_file_of_nothing_fails) {
    test::TempDir dir;
    auto content = read_file(dir.file("missing"));
    ZASSERT(!content.has_value());
    ZEXPECT(content.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(read_file_of_a_directory_fails) {
    test::TempDir dir;
    auto content = read_file(dir.root());
    ZASSERT(!content.has_value());
#ifdef _WIN32
    // Windows opens a directory only for its metadata.
    ZEXPECT(content.error() == std::errc::permission_denied);
#else
    ZEXPECT(content.error() == std::errc::is_a_directory);
#endif
}

ZEST_CASE(open_reads_and_writes_through_the_handle) {
    test::TempDir dir;
    auto file = File::open(dir.file("file"), OpenMode::Write);
    ZASSERT(file.has_value());
    ZEXPECT(file->is_open());
    ZEXPECT(!file->write("hello"));
    ZEXPECT(!file->close());
    ZEXPECT(!file->is_open());

    auto reader = File::open(dir.file("file"), OpenMode::Read);
    ZASSERT(reader.has_value());
    std::array<char, 16> buffer{};
    auto count = reader->read(buffer);
    ZASSERT(count.has_value());
    ZEXPECT(std::string(buffer.data(), *count) == "hello");
    ZEXPECT(reader->read(buffer) == 0u);
}

ZEST_CASE(open_to_create_new_over_anything_fails) {
    test::TempDir dir;
    ZEXPECT(File::open(dir.file("new"), OpenMode::CreateNew).has_value());
    auto again = File::open(dir.file("new"), OpenMode::CreateNew);
    ZASSERT(!again.has_value());
    ZEXPECT(again.error() == std::errc::file_exists);
}

ZEST_CASE(open_to_create_new_over_a_dangling_link_fails) {
    test::TempDir dir;
    if(auto error = create_symlink("missing", dir.file("link"))) {
        // Windows lets only an administrator or developer mode make links.
        ZEXPECT(error == std::errc::operation_not_permitted);
        zest::skip();
        return;
    }
    auto file = File::open(dir.file("link"), OpenMode::CreateNew);
    ZASSERT(!file.has_value());
    ZEXPECT(file.error() == std::errc::file_exists);
    ZEXPECT(!is_file(dir.file("missing")));
}

ZEST_CASE(write_file_atomic_replaces_the_whole_file) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "old content");
    ZEXPECT(!write_file_atomic(dir.file("file"), "new"));
    ZEXPECT(test::read_text(dir.file("file")) == "new");
    // The sibling it wrote through is gone.
    auto entries = read_dir(dir.root());
    ZASSERT(entries.has_value());
    ZEXPECT(entries->size() == 1u);
}

ZEST_CASE(create_temp_file_names_a_new_empty_file) {
    test::TempDir dir;
    std::set<std::string> made;
    for(int i = 0; i < 8; ++i) {
        auto created = create_temp_file(dir.root(), "pre-", ".tmp");
        ZASSERT(created.has_value());
        ZEXPECT(path::filename(*created).starts_with("pre-"));
        ZEXPECT(path::extension(*created) == ".tmp");
        ZEXPECT(read_file(*created) == std::string());
        made.insert(*created);
    }
    ZEXPECT(made.size() == 8u);
}

ZEST_CASE(create_temp_file_in_nothing_fails) {
    test::TempDir dir;
    auto created = create_temp_file(dir.file("missing"), "pre-", "");
    ZASSERT(!created.has_value());
    ZEXPECT(created.error() == std::errc::no_such_file_or_directory);
}

};  // ZEST_SUITE(fs_file)

}  // namespace

}  // namespace kota::fs
