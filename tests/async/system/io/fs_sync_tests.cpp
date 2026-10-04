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
    ZASSERT(out.has_value());
    auto written = fs::sync::write(*out, std::string_view("hello world"));
    ZASSERT(written.has_value());
    ZEXPECT(*written == 11U);
    auto patched = fs::sync::write(*out, std::string_view("WORLD"), 6);
    ZASSERT(patched.has_value());
    ZEXPECT(!fs::sync::close(*out));

    auto whole = fs::sync::read_to_string(path);
    ZASSERT(whole.has_value());
    ZEXPECT(*whole == "hello WORLD");

    auto in = fs::sync::open(path, O_RDONLY);
    ZASSERT(in.has_value());
    std::array<char, 5> buffer{};
    auto count = fs::sync::read(*in, buffer, 6);
    ZEXPECT(!fs::sync::close(*in));
    ZASSERT(count.has_value());
    ZEXPECT(std::string(buffer.data(), *count) == "WORLD");
}

// read_to_string reads in 4 KiB pieces.
ZEST_CASE(read_to_string_reads_a_large_file_whole) {
    test::TempDir dir;
    std::string large(10'000, 'k');
    large.back() = 'z';
    test::write_file(dir.path / "large.txt", large);

    auto whole = fs::sync::read_to_string(dir.file("large.txt"));
    ZASSERT(whole.has_value());
    ZEXPECT(whole->size() == large.size());
    ZEXPECT(*whole == large);
}

ZEST_CASE(missing_file_fails) {
    test::TempDir dir;

    auto opened = fs::sync::open(dir.file("missing"), O_RDONLY);
    ZASSERT(opened.has_error());
    ZEXPECT(opened.error() == error::no_such_file_or_directory);

    auto whole = fs::sync::read_to_string(dir.file("missing"));
    ZASSERT(whole.has_error());
    ZEXPECT(whole.error() == error::no_such_file_or_directory);
}

// A directory opens, then fails on the first read; Windows answers that
// read with a code of its own.
#ifndef _WIN32
ZEST_CASE(read_to_string_of_a_directory_fails) {
    test::TempDir dir;
    auto whole = fs::sync::read_to_string(dir.path.string());
    ZASSERT(whole.has_error());
    ZEXPECT(whole.error() == error::illegal_operation_on_a_directory);
}
#endif

ZEST_CASE(bad_descriptor_fails) {
    std::array<char, 8> buffer{};
    auto count = fs::sync::read(-1, buffer);
    ZASSERT(count.has_error());
    ZEXPECT(count.error() == error::bad_file_descriptor);
    auto written = fs::sync::write(-1, std::string_view("x"));
    ZASSERT(written.has_error());
    ZEXPECT(written.error() == error::bad_file_descriptor);
}

};  // ZEST_SUITE(async_io_fs_sync)

}  // namespace

}  // namespace kota
