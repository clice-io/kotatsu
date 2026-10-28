#include <expected>
#include <iterator>
#include <ranges>
#include <system_error>
#include <utility>

#include "kota/zest/zest.h"
#include "kota/fs/directory.h"

namespace kota::fs {

namespace {

ZEST_SUITE(fs_directory_range) {

ZEST_CASE(directory_is_an_input_range) {
    // The standard library's views and algorithms take it.
    ZSTATIC_EXPECT(std::input_iterator<Directory::Iterator>);
    ZSTATIC_EXPECT(std::sentinel_for<std::default_sentinel_t, Directory::Iterator>);
    ZSTATIC_EXPECT(std::ranges::input_range<Directory>);
    ZSTATIC_EXPECT(std::ranges::viewable_range<Directory&>);
    ZSTATIC_EXPECT(std::ranges::viewable_range<Directory>);
    ZSTATIC_EXPECT(
        std::ranges::input_range<decltype(std::declval<Directory&>() | std::views::take(1))>);
    // An entry may be moved out.
    ZEXPECT(zest::type_eq<std::iter_reference_t<Directory::Iterator>,
                          std::expected<Directory::Entry, std::error_code>&>());
    // Gone through once: no copy shares its place.
    ZSTATIC_EXPECT(!std::copyable<Directory>);
    ZSTATIC_EXPECT(std::movable<Directory>);
}

ZEST_CASE(walk_is_an_input_range) {
    ZSTATIC_EXPECT(std::input_iterator<Walk::Iterator>);
    ZSTATIC_EXPECT(std::sentinel_for<std::default_sentinel_t, Walk::Iterator>);
    ZSTATIC_EXPECT(std::ranges::input_range<Walk>);
    ZSTATIC_EXPECT(std::ranges::viewable_range<Walk>);
    ZEXPECT(zest::type_eq<std::iter_reference_t<Walk::Iterator>,
                          std::expected<Walk::Entry, Walk::Error>&>());
    ZSTATIC_EXPECT(!std::copyable<Walk>);
    ZSTATIC_EXPECT(std::movable<Walk>);
}

ZEST_CASE(entry_status_of_one_made_rather_than_listed_fails) {
    // No directory to ask through, nor a listing that told it.
    Directory::Entry entry;
    auto status = entry.status();
    ZASSERT(!status.has_value());
    ZEXPECT(status.error() == std::errc::bad_file_descriptor);
}

};  // ZEST_SUITE(fs_directory_range)

}  // namespace

}  // namespace kota::fs
