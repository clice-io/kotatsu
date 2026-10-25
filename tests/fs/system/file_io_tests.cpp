#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "fs/harness/files.h"
#include "fs/harness/win32.h"
#include "kota/zest/zest.h"
#include "kota/support/small_string.h"
#include "kota/fs/file.h"

namespace kota::fs {

namespace {

using namespace std::literals;

/// The write end of a pipe set not to wait, as a File, with its read end
/// left unread so that it fills; both closed when this goes.
struct FullPipe {
    FullPipe() {
#ifdef _WIN32
        std::string name;
        reader = test::make_pipe(name, dir, PIPE_ACCESS_INBOUND, 4096);
        auto opened = File::open(name, {.write = true});
        ZASSERT(opened.has_value());
        writer = std::move(*opened);
        DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
        ZASSERT(::SetNamedPipeHandleState(writer.native_handle(), &mode, nullptr, nullptr));
#else
        auto fifo = dir.file("fifo");
        ZASSERT(::mkfifo(fifo.c_str(), 0600) == 0);
        // A reader there already lets the writer open without waiting.
        reader = ::open(fifo.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        ZASSERT(reader >= 0);
        auto opened = File::open(fifo, {.write = true});
        ZASSERT(opened.has_value());
        writer = std::move(*opened);
        int flags = ::fcntl(writer.native_handle(), F_GETFL);
        ZASSERT(::fcntl(writer.native_handle(), F_SETFL, flags | O_NONBLOCK) == 0);
#endif
    }

    ~FullPipe() {
#ifdef _WIN32
        ::CloseHandle(reader);
#else
        ::close(reader);
#endif
    }

    FullPipe(const FullPipe&) = delete;
    FullPipe& operator=(const FullPipe&) = delete;

    /// Writes until the pipe takes nothing more: the error that says so.
    std::error_code fill() {
        std::string chunk(4096, 'x');
        for(int i = 0; i < 1024; ++i) {
            auto count = writer.write_some(chunk);
            if(!count) {
                return count.error();
            }
        }
        return {};
    }

    test::TempDir dir;
    File::native_handle_type reader;
    File writer;
};

/// A buffer that cuts the file at `path` to "text" when it is first sized:
/// a file that holds less than it reported.
struct ShrinkingBuffer {
    char* data() {
        return text.data();
    }

    std::size_t size() const {
        return text.size();
    }

    void resize(std::size_t size) {
        if(first == 0) {
            first = size;
            test::write_text(path, "text");
        }
        text.resize(size);
    }

    std::string path;
    std::string text;

    /// The size it was first asked for.
    std::size_t first = 0;
};

ZEST_SUITE(fs_file_io) {

ZEST_CASE(read_to_end_reads_from_where_the_file_stands) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "hello world");
    auto file = File::open(dir.file("file"));
    ZASSERT(file.has_value());
    std::array<char, 6> head{};
    ZEXPECT(file->read(head) == 6u);
    std::string rest;
    ZEXPECT(!file->read_to_end(rest));
    ZEXPECT(rest == "world");
    ZEXPECT(!file->read_to_end(rest));
    ZEXPECT(rest == "");
}

ZEST_CASE(read_to_end_writes_over_what_the_buffer_held) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "new\0text"sv);
    auto file = File::open(dir.file("file"));
    ZASSERT(file.has_value());
    small_string<4> content("some old text");
    ZEXPECT(!file->read_to_end(content));
    ZEXPECT(std::string_view(content) == "new\0text"sv);
}

ZEST_CASE(read_to_end_sizes_its_buffer_once) {
    test::TempDir dir;
    auto text = test::text_of(1'000'000);
    test::write_text(dir.file("large"), text);
    auto file = File::open(dir.file("large"));
    ZASSERT(file.has_value());
    std::array<char, 1000> head{};
    ZEXPECT(file->read(head) == 1000u);
    test::CountedBuffer content;
    ZEXPECT(!file->read_to_end(content));
    ZEXPECT(content.text == std::string_view(text).substr(1000));
    // Sized for what is left, then cut to what was read.
    ZEXPECT(content.resizes == 2);
}

ZEST_CASE(read_to_end_takes_no_size_past_a_gigabyte_on_its_word) {
    // A file may report more than it holds (/proc/kcore reports 128 TiB):
    // the buffer starts small and grows as it is read.
    test::TempDir dir;
    auto file = File::open(dir.file("sparse"), {.read = true, .write = true, .create = true});
    ZASSERT(file.has_value());
    ZEXPECT(!file->resize(std::uint64_t{2} << 30));
    ShrinkingBuffer content;
    content.path = dir.file("sparse");
    ZEXPECT(!file->read_to_end(content));
    ZEXPECT(content.text == "text");
    ZEXPECT(content.first < (std::size_t{1} << 30));
}

ZEST_CASE(read_to_end_reads_a_pipe_whole) {
    // A pipe reports no size: the buffer grows as it is read.
    test::TempDir dir;
    auto text = test::text_of(10'000);
    std::string content;
#ifdef _WIN32
    std::string name;
    HANDLE writer = test::make_pipe(name, dir, PIPE_ACCESS_OUTBOUND, 65536);
    auto file = File::open(name);
    ZASSERT(file.has_value());
    DWORD written = 0;
    ZEXPECT(::WriteFile(writer, text.data(), static_cast<DWORD>(text.size()), &written, nullptr));
    // The bytes stay to read once the writer is gone.
    ::CloseHandle(writer);
    ZEXPECT(!file->read_to_end(content));
#else
    auto fifo = dir.file("fifo");
    ZASSERT(::mkfifo(fifo.c_str(), 0600) == 0);
    // Each end's open waits for the other's.
    ssize_t written = 0;
    std::thread writer([&] {
        int fd = ::open(fifo.c_str(), O_WRONLY | O_CLOEXEC);
        written = ::write(fd, text.data(), text.size());
        ::close(fd);
    });
    auto file = File::open(fifo);
    ZASSERT(file.has_value());
    auto error = file->read_to_end(content);
    writer.join();
    ZEXPECT(!error);
    ZEXPECT(written == static_cast<ssize_t>(text.size()));
#endif
    ZEXPECT(content == text);
}

ZEST_CASE(read_to_end_of_a_file_open_to_write_fails) {
    test::TempDir dir;
    auto file = File::open(dir.file("file"), {.write = true, .create = true});
    ZASSERT(file.has_value());
    std::string content;
    auto error = file->read_to_end(content);
#ifdef _WIN32
    ZEXPECT(error == std::errc::permission_denied);
#else
    ZEXPECT(error == std::errc::bad_file_descriptor);
#endif
}

ZEST_CASE(write_some_writes_from_the_start) {
    test::TempDir dir;
    auto file = File::open(dir.file("file"), {.write = true, .create = true});
    ZASSERT(file.has_value());
    ZEXPECT(file->write_some("hello") == 5u);
    ZEXPECT(file->write_some("") == 0u);
    ZEXPECT(file->write_some(" world") == 6u);
    ZEXPECT(!file->close());
    ZEXPECT(test::read_text(dir.file("file")) == "hello world");
}

ZEST_CASE(write_some_to_a_full_pipe_fails) {
    FullPipe pipe;
    ZEXPECT(pipe.fill() == std::errc::resource_unavailable_try_again);
}

ZEST_CASE(write_to_a_full_pipe_fails) {
    // Windows calls a write that took nothing a success: taken for progress,
    // it would write forever.
    FullPipe pipe;
    ZEXPECT(pipe.fill() == std::errc::resource_unavailable_try_again);
    ZEXPECT(pipe.writer.write("more") == std::errc::resource_unavailable_try_again);
}

ZEST_CASE(write_some_of_a_closed_file_fails) {
    File file;
    auto count = file.write_some("text");
    ZASSERT(!count.has_value());
    ZEXPECT(count.error() == std::errc::bad_file_descriptor);
}

};  // ZEST_SUITE(fs_file_io)

}  // namespace

}  // namespace kota::fs
