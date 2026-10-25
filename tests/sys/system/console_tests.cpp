#include <array>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/sys/console.h"

namespace kota::sys {

namespace {

/// A pipe's two descriptors, closed when it goes.
struct Pipe {
    Pipe() {
#ifdef _WIN32
        ZASSERT(::_pipe(fds.data(), 1 << 16, _O_BINARY) == 0);
#else
        ZASSERT(::pipe(fds.data()) == 0);
#endif
    }

    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;

    ~Pipe() {
        close_write();
#ifdef _WIN32
        ::_close(fds[0]);
#else
        ::close(fds[0]);
#endif
    }

    void close_write() {
        if(fds[1] >= 0) {
#ifdef _WIN32
            ::_close(fds[1]);
#else
            ::close(fds[1]);
#endif
            fds[1] = -1;
        }
    }

    /// Writes to the write end return at once, taking what fits: O_NONBLOCK,
    /// or Windows' PIPE_NOWAIT.
    void stop_blocking_writes() {
#ifdef _WIN32
        DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
        auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fds[1]));
        ZASSERT(::SetNamedPipeHandleState(handle, &mode, nullptr, nullptr));
#else
        auto flags = ::fcntl(fds[1], F_GETFL);
        ZASSERT(flags >= 0);
        ZASSERT(::fcntl(fds[1], F_SETFL, flags | O_NONBLOCK) == 0);
#endif
    }

    /// Everything written, until the write end is closed.
    std::string read_all() {
        std::string text;
        char buffer[4096];
        while(true) {
#ifdef _WIN32
            auto count = ::_read(fds[0], buffer, sizeof(buffer));
#else
            auto count = ::read(fds[0], buffer, sizeof(buffer));
#endif
            if(count <= 0) {
                return text;
            }
            text.append(buffer, static_cast<std::size_t>(count));
        }
    }

    std::array<int, 2> fds{-1, -1};
};

ZEST_SUITE(sys_console) {

// A pipe is no console: the bytes go through as they are, ill-formed or not.
ZEST_CASE(write_console_writes_bytes_to_a_pipe) {
    Pipe pipe;
    std::string_view text = "caf\xC3\xA9 \xF0\x9F\x98\x80 \xFF\xC3 end\n";
    ZEXPECT(!write_console(pipe.fds[1], text));
    pipe.close_write();
    ZEXPECT(pipe.read_all() == text);
}

// The pipe takes the text a part at a time, as the other end reads.
ZEST_CASE(write_console_writes_all_of_a_long_text) {
    Pipe pipe;
    std::string text(1 << 20, 'x');
    std::string read;
    std::thread reader([&] { read = pipe.read_all(); });
    auto error = write_console(pipe.fds[1], text);
    pipe.close_write();
    reader.join();
    ZEXPECT(!error);
    ZEXPECT(read.size() == text.size());
}

// A pipe that does not block takes what fits, nobody reading, and then
// nothing: Windows' WriteFile says so by writing no byte, which must end the
// write rather than repeat it for ever. Filled a byte at a time, as Windows
// may take nothing of a larger write.
ZEST_CASE(write_console_to_a_full_pipe_that_does_not_block_fails) {
    Pipe pipe;
    pipe.stop_blocking_writes();
    std::size_t taken = 0;
    std::error_code error;
    while(!(error = write_console(pipe.fds[1], "x"))) {
        ++taken;
    }
    ZEXPECT(error == std::errc::resource_unavailable_try_again);
    ZEXPECT(taken > 0U);
    pipe.close_write();
    ZEXPECT(pipe.read_all().size() == taken);
}

#ifndef _WIN32
// The CRT of Windows aborts on a descriptor it never opened.
ZEST_CASE(write_console_to_no_descriptor_fails) {
    ZEXPECT(write_console(-1, "text") == std::errc::bad_file_descriptor);
}
#endif

};  // ZEST_SUITE(sys_console)

}  // namespace

}  // namespace kota::sys
