#include <filesystem>
#include <system_error>

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/fs/directory.h"
#include "kota/fs/volume.h"

namespace kota::fs {

namespace {

ZEST_SUITE(fs_volume) {

ZEST_CASE(volume_tells_space_and_kind) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto found = volume(dir.file("file"));
    ZASSERT(found.has_value());
    ZEXPECT(found->capacity > 0u);
    ZEXPECT(found->free <= found->capacity);
    ZEXPECT(found->available <= found->free);
    ZEXPECT(!found->type.empty());
    // The system temp directory of the machines the suite runs on is local.
    ZEXPECT(!found->remote);
}

ZEST_CASE(volume_tells_the_capacity_the_standard_library_does) {
    test::TempDir dir;
    auto found = volume(dir.root());
    ZASSERT(found.has_value());
    ZEXPECT(found->capacity == std::filesystem::space(dir.path).capacity);
}

#ifdef _WIN32
ZEST_CASE(volume_of_a_share_is_remote) {
    // The system drive's administrative share, through the SMB client.
    auto found = volume(R"(\\localhost\C$)");
    if(!found) {
        // No administrative shares where the suite runs.
        zest::skip();
        return;
    }
    ZEXPECT(found->remote);
    auto directory = Directory::open(R"(\\localhost\C$)");
    ZASSERT(directory.has_value());
    auto listed = directory->volume();
    ZASSERT(listed.has_value());
    ZEXPECT(listed->remote);
}
#endif

#ifdef __linux__
ZEST_CASE(volume_names_a_kind_linux_knows) {
    auto found = volume("/proc/self");
    ZASSERT(found.has_value());
    ZEXPECT(found->type == "proc");
}
#endif

ZEST_CASE(directory_volume_matches_the_volume_of_its_path) {
    test::TempDir dir;
    auto named = volume(dir.root());
    auto directory = Directory::open(dir.root());
    ZASSERT(named.has_value());
    ZASSERT(directory.has_value());
    auto listed = directory->volume();
    ZASSERT(listed.has_value());
    ZEXPECT(listed->type == named->type);
    ZEXPECT(listed->capacity == named->capacity);
    ZEXPECT(listed->remote == named->remote);
}

ZEST_CASE(volume_of_nothing_fails) {
    test::TempDir dir;
    auto found = volume(dir.file("missing"));
    ZASSERT(!found.has_value());
    ZEXPECT(found.error() == std::errc::no_such_file_or_directory);
}

};  // ZEST_SUITE(fs_volume)

}  // namespace

}  // namespace kota::fs
