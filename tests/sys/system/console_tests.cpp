#include <array>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <cstdint>
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
#include <cstdlib>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "fs/harness/files.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/sys/console.h"

namespace kota::sys {

namespace {

void close_descriptor(int fd) {
#ifdef _WIN32
    ::_close(fd);
#else
    ::close(fd);
#endif
}

/// A descriptor, closed when it goes.
struct Descriptor {
    explicit Descriptor(int fd) : fd(fd) {}

    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;

    ~Descriptor() {
        if(fd >= 0) {
            close_descriptor(fd);
        }
    }

    int fd;
};

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
        close_descriptor(fds[0]);
    }

    void close_write() {
        if(fds[1] >= 0) {
            close_descriptor(fds[1]);
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

/// A descriptor of file `path`, open for reading.
int open_file(const std::string& path) {
#ifdef _WIN32
    return ::_wopen(test::std_path(path).c_str(), _O_RDONLY | _O_BINARY);
#else
    return ::open(path.c_str(), O_RDONLY);
#endif
}

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

ZEST_CASE(write_console_to_no_descriptor_fails) {
    ZEXPECT(write_console(-1, "text") == std::errc::bad_file_descriptor);
}

ZEST_CASE(is_pipe_of_a_pipe) {
    Pipe pipe;
    ZEXPECT(is_pipe(pipe.fds[0]));
    ZEXPECT(is_pipe(pipe.fds[1]));
    ZEXPECT(!is_console(pipe.fds[1]));
}

#ifndef _WIN32
ZEST_CASE(is_pipe_of_a_socket) {
    int fds[2];
    ZASSERT(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    Descriptor one(fds[0]);
    Descriptor other(fds[1]);
    ZEXPECT(is_pipe(one.fd));
    ZEXPECT(!is_console(one.fd));
}
#endif

ZEST_CASE(is_pipe_of_a_file_is_false) {
    test::TempDir dir;
    auto path = dir.file("file");
    test::write_text(path, "text");
    Descriptor file(open_file(path));
    ZASSERT(file.fd >= 0);
    ZEXPECT(!is_pipe(file.fd));
    ZEXPECT(!is_console(file.fd));
}

// A device of characters, which Windows' _isatty takes for a terminal.
ZEST_CASE(is_console_of_the_null_device_is_false) {
#ifdef _WIN32
    Descriptor null(::_open("NUL", _O_WRONLY | _O_BINARY));
#else
    Descriptor null(::open("/dev/null", O_WRONLY));
#endif
    ZASSERT(null.fd >= 0);
    ZEXPECT(!is_console(null.fd));
    ZEXPECT(!is_pipe(null.fd));
}

// Neither ends the process on Windows, as the C runtime's check would.
ZEST_CASE(is_pipe_of_no_descriptor_is_false) {
    int closed = -1;
    {
        Pipe pipe;
        closed = pipe.fds[0];
    }
    for(int fd: {-1, closed}) {
        ZEST_CONTEXT("descriptor {}", fd);
        ZEXPECT(!is_pipe(fd));
        ZEXPECT(!is_console(fd));
    }
}

ZEST_CASE(is_console_of_a_terminal) {
#ifdef _WIN32
    // The console this process has; none when it was started without one.
    HANDLE console = ::CreateFileW(L"CONOUT$",
                                   GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr,
                                   OPEN_EXISTING,
                                   0,
                                   nullptr);
    if(console == INVALID_HANDLE_VALUE) {
        zest::skip();
        return;
    }
    Descriptor terminal(::_open_osfhandle(reinterpret_cast<std::intptr_t>(console), _O_BINARY));
#else
    // A pseudo-terminal's other end, which a sandbox may not give.
    Descriptor master(::posix_openpt(O_RDWR | O_NOCTTY));
    if(master.fd < 0 || ::grantpt(master.fd) != 0 || ::unlockpt(master.fd) != 0) {
        zest::skip();
        return;
    }
    Descriptor terminal(::open(::ptsname(master.fd), O_RDWR | O_NOCTTY));
#endif
    ZASSERT(terminal.fd >= 0);
    ZEXPECT(is_console(terminal.fd));
    ZEXPECT(!is_pipe(terminal.fd));
}

};  // ZEST_SUITE(sys_console)

}  // namespace

}  // namespace kota::sys
