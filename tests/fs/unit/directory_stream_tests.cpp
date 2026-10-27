#include <expected>
#include <iterator>
#include <ranges>
#include <system_error>
#include <utility>

#include "kota/zest/zest.h"
#include "kota/fs/directory.h"

namespace kota::fs {

namespace {

ZEST_SUITE(fs_directory_stream) {

ZEST_CASE(dir_stream_is_an_input_range) {
    // The standard library's views and algorithms take it.
    ZSTATIC_EXPECT(std::input_iterator<DirStream::iterator>);
    ZSTATIC_EXPECT(std::sentinel_for<std::default_sentinel_t, DirStream::iterator>);
    ZSTATIC_EXPECT(std::ranges::input_range<DirStream>);
    ZSTATIC_EXPECT(std::ranges::viewable_range<DirStream&>);
    ZSTATIC_EXPECT(std::ranges::viewable_range<DirStream>);
    ZSTATIC_EXPECT(
        std::ranges::input_range<decltype(std::declval<DirStream&>() | std::views::take(1))>);
    // An entry may be moved out.
    ZEXPECT(zest::type_eq<std::iter_reference_t<DirStream::iterator>,
                          std::expected<DirEntry, std::error_code>&>());
    // Gone through once: no copy shares its place.
    ZSTATIC_EXPECT(!std::copyable<DirStream>);
    ZSTATIC_EXPECT(std::movable<DirStream>);
}

ZEST_CASE(dir_entry_made_rather_than_listed_has_no_status_fails) {
    // No directory to ask through, nor a listing that told it.
    DirEntry entry;
    auto status = entry.status();
    ZASSERT(!status.has_value());
    ZEXPECT(status.error() == std::errc::bad_file_descriptor);
}

};  // ZEST_SUITE(fs_directory_stream)

}  // namespace

}  // namespace kota::fs
