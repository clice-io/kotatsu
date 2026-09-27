#include <array>
#include <fcntl.h>
#include <string>
#include <string_view>

#include "async/harness/os.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_io_fs_sync) {

ZEST_CASE(write_then_read_back) {
    test::TempDir dir;
    auto path = dir.file("data.txt");

    auto out = fs::sync::open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    ASSERT(out.has_value());
    auto written = fs::sync::write(*out, std::string_view("hello world"));
    ASSERT(written.has_value());
    EXPECT(*written == 11U);
    auto patched = fs::sync::write(*out, std::string_view("WORLD"), 6);
    ASSERT(patched.has_value());
    EXPECT(!fs::sync::close(*out));

    auto whole = fs::sync::read_to_string(path);
    ASSERT(whole.has_value());
    EXPECT(*whole == "hello WORLD");

    auto in = fs::sync::open(path, O_RDONLY);
    ASSERT(in.has_value());
    std::array<char, 5> buffer{};
    auto count = fs::sync::read(*in, buffer, 6);
    EXPECT(!fs::sync::close(*in));
    ASSERT(count.has_value());
    EXPECT(std::string(buffer.data(), *count) == "WORLD");
}

// read_to_string reads in 4 KiB pieces.
ZEST_CASE(read_to_string_reads_a_large_file_whole) {
    test::TempDir dir;
    std::string large(10'000, 'k');
    large.back() = 'z';
    test::write_file(dir.path / "large.txt", large);

    auto whole = fs::sync::read_to_string(dir.file("large.txt"));
    ASSERT(whole.has_value());
    EXPECT(whole->size() == large.size());
    EXPECT(*whole == large);
}

ZEST_CASE(missing_file_fails) {
    test::TempDir dir;

    auto opened = fs::sync::open(dir.file("missing"), O_RDONLY);
    ASSERT(opened.has_error());
    EXPECT(opened.error() == error::no_such_file_or_directory);

    auto whole = fs::sync::read_to_string(dir.file("missing"));
    ASSERT(whole.has_error());
    EXPECT(whole.error() == error::no_such_file_or_directory);
}

// A directory opens, then fails on the first read; Windows answers that
// read with a code of its own.
#ifndef _WIN32
ZEST_CASE(read_to_string_of_a_directory_fails) {
    test::TempDir dir;
    auto whole = fs::sync::read_to_string(dir.path.string());
    ASSERT(whole.has_error());
    EXPECT(whole.error() == error::illegal_operation_on_a_directory);
}
#endif

ZEST_CASE(bad_descriptor_fails) {
    std::array<char, 8> buffer{};
    auto count = fs::sync::read(-1, buffer);
    ASSERT(count.has_error());
    EXPECT(count.error() == error::bad_file_descriptor);
    auto written = fs::sync::write(-1, std::string_view("x"));
    ASSERT(written.has_error());
    EXPECT(written.error() == error::bad_file_descriptor);
}

};  // ZEST_SUITE(async_io_fs_sync)

}  // namespace

}  // namespace kota
