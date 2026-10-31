#include <string>
#include <system_error>
#include <utility>

#include "fs/harness/files.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/fs/directory.h"
#include "kotatsu/fs/file.h"
#include "kotatsu/fs/mapping.h"
#include "kotatsu/fs/status.h"

namespace kotatsu::fs {

namespace {

ZEST_SUITE(fs_mapping) {

ZEST_CASE(map_shows_the_files_bytes) {
    test::TempDir dir;
    auto content = test::text_of(100'000);
    test::write_text(dir.file("é中😀"), content);
    auto file = File::open(dir.file("é中😀"));
    ZASSERT(file.has_value());
    auto mapping = Mapping::map(*file);
    ZASSERT(mapping.has_value());
    // The mapping outlives the handle.
    ZEXPECT(!file->close());
    ZEXPECT(mapping->bytes() == content);
}

ZEST_CASE(map_of_an_empty_file_has_no_bytes) {
    test::TempDir dir;
    test::write_text(dir.file("empty"), "");
    auto file = File::open(dir.file("empty"));
    ZASSERT(file.has_value());
    auto mapping = Mapping::map(*file);
    ZASSERT(mapping.has_value());
    ZEXPECT(mapping->bytes().empty());
}

#ifdef __linux__
ZEST_CASE(map_of_a_file_without_a_size_has_no_bytes) {
    auto file = File::open("/proc/self/status");
    ZASSERT(file.has_value());
    auto mapping = Mapping::map(*file);
    ZASSERT(mapping.has_value());
    ZEXPECT(mapping->bytes().empty());
}
#endif

ZEST_CASE(a_moved_mapping_keeps_its_bytes) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto file = File::open(dir.file("file"));
    ZASSERT(file.has_value());
    auto mapping = Mapping::map(*file);
    ZASSERT(mapping.has_value());
    Mapping moved(std::move(*mapping));
    ZEXPECT(mapping->bytes().empty());
    ZEXPECT(moved.bytes() == "text");
    Mapping assigned;
    assigned = std::move(moved);
    ZEXPECT(moved.bytes().empty());
    ZEXPECT(assigned.bytes() == "text");
}

ZEST_CASE(remove_takes_a_mapped_file) {
    // A cache that removes a PCH another process keeps mapped.
    test::TempDir dir;
    std::string content(64 * 1024, 'x');
    test::write_text(dir.file("mapped"), content);
    auto mapping = File::open(dir.file("mapped")).and_then(Mapping::map);
    ZASSERT(mapping.has_value());
    ZEXPECT(remove(dir.file("mapped")) == true);
    ZEXPECT(try_exists(dir.file("mapped")) == false);
    ZEXPECT(mapping->bytes() == content);
    // The name is free for a new file at once.
    ZEXPECT(!write_file(dir.file("mapped"), "new"));
    ZEXPECT(test::read_text(dir.file("mapped")) == "new");
}

ZEST_CASE(a_mapped_file_can_be_replaced) {
    test::TempDir dir;
    std::string content(64 * 1024, 'x');
    test::write_text(dir.file("mapped"), content);
    auto file = File::open(dir.file("mapped"));
    ZASSERT(file.has_value());
    auto mapping = Mapping::map(*file);
    ZASSERT(mapping.has_value());
    ZEXPECT(!write_file_atomic(dir.file("mapped"), "new"));
    ZEXPECT(test::read_text(dir.file("mapped")) == "new");
    test::write_text(dir.file("newer"), "newer");
    ZEXPECT(!rename(dir.file("newer"), dir.file("mapped")));
    ZEXPECT(test::read_text(dir.file("mapped")) == "newer");
    // The mapping keeps the file it mapped.
    ZEXPECT(mapping->bytes() == content);
}

ZEST_CASE(map_of_a_file_open_to_write_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto file = File::open(dir.file("file"), {.write = true});
    ZASSERT(file.has_value());
    auto mapping = Mapping::map(*file);
    ZASSERT(!mapping.has_value());
    ZEXPECT(mapping.error() == std::errc::permission_denied);
}

ZEST_CASE(map_of_a_closed_file_fails) {
    auto mapping = Mapping::map(File());
    ZASSERT(!mapping.has_value());
    ZEXPECT(mapping.error() == std::errc::bad_file_descriptor);
}

};  // ZEST_SUITE(fs_mapping)

}  // namespace

}  // namespace kotatsu::fs
