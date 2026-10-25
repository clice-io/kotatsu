#include <iterator>
#include <ranges>
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
    // Gone through once: no copy shares its place.
    ZSTATIC_EXPECT(!std::copyable<DirStream>);
    ZSTATIC_EXPECT(std::movable<DirStream>);
}

};  // ZEST_SUITE(fs_directory_stream)

}  // namespace

}  // namespace kota::fs
