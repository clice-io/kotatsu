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
#include <winsock2.h>
#else
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "fs/harness/files.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/sys/console.h"

namespace kotatsu::sys {

namespace {

/// A descriptor, closed when it goes.
struct Descriptor {
    explicit Descriptor(int fd) : fd(fd) {}

    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;

    ~Descriptor() {
        reset();
    }

    void reset() {
        if(fd >= 0) {
#ifdef _WIN32
            ::_close(fd);
#else
            ::close(fd);
#endif
            fd = -1;
        }
    }

    int fd;
};

/// A pipe's two ends.
struct Pipe {
    Pipe() {
        int fds[2];
#ifdef _WIN32
        ZASSERT(::_pipe(fds, 1 << 16, _O_BINARY) == 0);
#else
        ZASSERT(::pipe(fds) == 0);
#endif
        read.fd = fds[0];
        write.fd = fds[1];
    }

    /// Everything written, until the write end is closed.
    std::string read_all() {
        std::string text;
        char buffer[4096];
        while(true) {
#ifdef _WIN32
            auto count = ::_read(read.fd, buffer, sizeof(buffer));
#else
            auto count = ::read(read.fd, buffer, sizeof(buffer));
#endif
            if(count <= 0) {
                return text;
            }
            text.append(buffer, static_cast<std::size_t>(count));
        }
    }

    Descriptor read{-1};
    Descriptor write{-1};
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
    ZEXPECT(!write_console(pipe.write.fd, text));
    pipe.write.reset();
    ZEXPECT(pipe.read_all() == text);
}

// The pipe takes the text a part at a time, as the other end reads.
ZEST_CASE(write_console_writes_all_of_a_long_text) {
    Pipe pipe;
    std::string text(1 << 20, 'x');
    std::string read;
    std::thread reader([&] { read = pipe.read_all(); });
    auto error = write_console(pipe.write.fd, text);
    pipe.write.reset();
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
    ZASSERT(!set_nonblocking(pipe.write.fd));
    std::size_t taken = 0;
    std::error_code error;
    while(!(error = write_console(pipe.write.fd, "x"))) {
        ++taken;
    }
    ZEXPECT(error == std::errc::resource_unavailable_try_again);
    ZEXPECT(taken > 0U);
    pipe.write.reset();
    ZEXPECT(pipe.read_all().size() == taken);
}

ZEST_CASE(write_console_some_counts_what_it_wrote) {
    Pipe pipe;
    auto written = write_console_some(pipe.write.fd, "text");
    ZASSERT(written);
    ZEXPECT(*written == 4U);
    pipe.write.reset();
    ZEXPECT(pipe.read_all() == "text");
}

// Each write that took a byte counts it, up to the one that took nothing.
ZEST_CASE(write_console_some_to_a_full_pipe_that_does_not_block_fails) {
    Pipe pipe;
    ZASSERT(!set_nonblocking(pipe.write.fd));
    std::size_t taken = 0;
    while(true) {
        auto written = write_console_some(pipe.write.fd, "xy");
        if(!written) {
            ZEXPECT(written.error() == std::errc::resource_unavailable_try_again);
            break;
        }
        ZASSERT(*written > 0U);
        taken += *written;
    }
    ZEXPECT(taken > 0U);
    pipe.write.reset();
    ZEXPECT(pipe.read_all().size() == taken);
}

#ifndef _WIN32
// What fits, where Windows may take nothing of a write larger than its room.
ZEST_CASE(write_console_some_takes_what_fits_in_a_pipe_that_does_not_block) {
    Pipe pipe;
    ZASSERT(!set_nonblocking(pipe.write.fd));
    std::string text(1 << 22, 'x');
    auto written = write_console_some(pipe.write.fd, text);
    ZASSERT(written);
    ZEXPECT(*written > 0U);
    ZEXPECT(*written < text.size());
}
#endif

// Without a write, which a descriptor not open would fail.
ZEST_CASE(write_console_some_of_nothing_writes_nothing) {
    auto written = write_console_some(-1, "");
    ZASSERT(written);
    ZEXPECT(*written == 0U);
}

ZEST_CASE(write_console_some_to_no_descriptor_fails) {
    auto written = write_console_some(-1, "text");
    ZASSERT(!written);
    ZEXPECT(written.error() == std::errc::bad_file_descriptor);
}

#ifdef _WIN32
/// The state GetNamedPipeHandleState reads of the pipe of descriptor `fd`.
DWORD pipe_state(int fd) {
    DWORD state = 0;
    auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
    ZASSERT(::GetNamedPipeHandleState(handle, &state, nullptr, nullptr, nullptr, nullptr, 0));
    return state;
}

ZEST_CASE(set_nonblocking_sets_pipe_nowait) {
    Pipe pipe;
    ZASSERT((pipe_state(pipe.write.fd) & PIPE_NOWAIT) == 0);
    ZEXPECT(!set_nonblocking(pipe.write.fd));
    ZEXPECT((pipe_state(pipe.write.fd) & PIPE_NOWAIT) != 0);
}

// A pipe of messages, whose name is the temporary directory's, stays one.
ZEST_CASE(set_nonblocking_keeps_a_pipes_read_mode) {
    test::TempDir dir;
    auto name = LR"(\\.\pipe\)" + dir.path.filename().wstring();
    HANDLE server = ::CreateNamedPipeW(name.c_str(),
                                       PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                       1,
                                       4096,
                                       4096,
                                       0,
                                       nullptr);
    ZASSERT(server != INVALID_HANDLE_VALUE);
    Descriptor pipe(::_open_osfhandle(reinterpret_cast<std::intptr_t>(server), 0));
    ZASSERT(pipe.fd >= 0);
    ZEXPECT(!set_nonblocking(pipe.fd));
    auto state = pipe_state(pipe.fd);
    ZEXPECT((state & PIPE_READMODE_MESSAGE) != 0);
    ZEXPECT((state & PIPE_NOWAIT) != 0);
}

ZEST_CASE(set_nonblocking_of_a_file_fails_on_windows) {
    test::TempDir dir;
    auto path = dir.file("file");
    test::write_text(path, "text");
    Descriptor file(open_file(path));
    ZASSERT(file.fd >= 0);
    ZEXPECT(set_nonblocking(file.fd) == std::errc::not_supported);
}

// Its read end, which takes no message mode of a byte pipe.
ZEST_CASE(set_nonblocking_of_a_pipes_read_end_lets_a_read_return_at_once) {
    Pipe pipe;
    ZASSERT(!set_nonblocking(pipe.read.fd));
    char byte;
    ZEXPECT(::_read(pipe.read.fd, &byte, 1) < 0);
}
#else
ZEST_CASE(set_nonblocking_sets_o_nonblock) {
    Pipe pipe;
    ZASSERT(!set_nonblocking(pipe.read.fd));
    ZEXPECT((::fcntl(pipe.read.fd, F_GETFL) & O_NONBLOCK) != 0);
    char byte;
    auto read = ::read(pipe.read.fd, &byte, 1);
    auto error = errno;
    ZEXPECT(read < 0);
    ZEXPECT(error == EAGAIN);
}
#endif

ZEST_CASE(set_nonblocking_of_no_descriptor_fails) {
    int closed = -1;
    {
        Pipe pipe;
        closed = pipe.write.fd;
    }
    for(int fd: {-1, closed}) {
        ZEST_CONTEXT("descriptor {}", fd);
        ZEXPECT(set_nonblocking(fd) == std::errc::bad_file_descriptor);
    }
}

// On Windows without ending the process, as the C runtime's check would.
ZEST_CASE(write_console_to_no_descriptor_fails) {
    int closed = -1;
    {
        Pipe pipe;
        closed = pipe.write.fd;
    }
    for(int fd: {-1, closed}) {
        ZEST_CONTEXT("descriptor {}", fd);
        ZEXPECT(write_console(fd, "text") == std::errc::bad_file_descriptor);
    }
}

ZEST_CASE(is_pipe_holds_for_a_pipe) {
    Pipe pipe;
    ZEXPECT(is_pipe(pipe.read.fd));
    ZEXPECT(is_pipe(pipe.write.fd));
    ZEXPECT(!is_console(pipe.write.fd));
}

ZEST_CASE(is_pipe_holds_for_a_socket) {
#ifdef _WIN32
    WSADATA data;
    ZASSERT(::WSAStartup(MAKEWORD(2, 2), &data) == 0);
    SOCKET handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ZASSERT(handle != INVALID_SOCKET);
    // Its descriptor's close closes the socket's handle too.
    Descriptor one(::_open_osfhandle(static_cast<std::intptr_t>(handle), 0));
    ZASSERT(one.fd >= 0);
#else
    int fds[2];
    ZASSERT(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    Descriptor one(fds[0]);
    Descriptor other(fds[1]);
#endif
    ZEXPECT(is_pipe(one.fd));
    ZEXPECT(!is_console(one.fd));
#ifdef _WIN32
    one.reset();
    ::WSACleanup();
#endif
}

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
        closed = pipe.read.fd;
    }
    for(int fd: {-1, closed}) {
        ZEST_CONTEXT("descriptor {}", fd);
        ZEXPECT(!is_pipe(fd));
        ZEXPECT(!is_console(fd));
    }
}

ZEST_CASE(is_console_holds_for_a_terminal) {
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
    ZASSERT(terminal.fd >= 0);
#else
    // A pseudo-terminal's other end, which a sandbox may not give.
    Descriptor master(::posix_openpt(O_RDWR | O_NOCTTY));
    const char* name = nullptr;
    if(master.fd >= 0 && ::grantpt(master.fd) == 0 && ::unlockpt(master.fd) == 0) {
        name = ::ptsname(master.fd);
    }
    Descriptor terminal(name == nullptr ? -1 : ::open(name, O_RDWR | O_NOCTTY));
    if(terminal.fd < 0) {
        zest::skip();
        return;
    }
#endif
    ZEXPECT(is_console(terminal.fd));
    ZEXPECT(!is_pipe(terminal.fd));
}

};  // ZEST_SUITE(sys_console)

}  // namespace

}  // namespace kotatsu::sys
