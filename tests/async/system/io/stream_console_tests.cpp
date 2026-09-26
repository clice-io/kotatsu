#include <cstddef>
#include <string>
#include <string_view>

#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include "async/harness/loop_fixture.h"
#include "async/harness/os.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

#ifndef _WIN32
/// A pseudo-terminal: the console under test runs on `terminal`, and the
/// test plays the user at `controller`.
struct PseudoTerminal {
    int controller = -1;
    int terminal = -1;

    PseudoTerminal() {
        controller = ::posix_openpt(O_RDWR | O_NOCTTY);
        if(controller < 0 || ::grantpt(controller) != 0 || ::unlockpt(controller) != 0) {
            return;
        }
        if(const char* name = ::ptsname(controller)) {
            terminal = ::open(name, O_RDWR | O_NOCTTY);
        }
    }

    PseudoTerminal(const PseudoTerminal&) = delete;
    PseudoTerminal& operator=(const PseudoTerminal&) = delete;

    ~PseudoTerminal() {
        if(terminal >= 0) {
            ::close(terminal);
        }
        if(controller >= 0) {
            ::close(controller);
        }
    }

    /// What the terminal has shown so far, waiting up to five seconds for
    /// the first byte.
    std::string shown() const {
        pollfd ready{controller, POLLIN, 0};
        if(::poll(&ready, 1, 5000) != 1) {
            return {};
        }
        char buffer[64];
        auto count = ::read(controller, buffer, sizeof(buffer));
        return count > 0 ? std::string(buffer, static_cast<std::size_t>(count)) : std::string();
    }
};
#endif

ZEST_SUITE(async_io_stream_console, test::LoopFixture) {

ZEST_CASE(reset_mode_without_a_raw_console_succeeds) {
    EXPECT(!console::reset_mode());
}

// Only Windows keeps a virtual terminal state.
ZEST_CASE(get_vterm_state_is_windows_only) {
    auto state = console::get_vterm_state();
#ifdef _WIN32
    EXPECT(state.has_value());
#else
    ASSERT(state.has_error());
    EXPECT(state.error() == error::operation_not_supported_on_socket);
#endif
}

#ifndef _WIN32
ZEST_CASE(open_on_a_regular_file_fails) {
    test::TempDir dir;
    test::write_file(dir.path / "plain.txt", "plain");
    int fd = ::open(dir.file("plain.txt").c_str(), O_RDONLY);
    ASSERT(fd >= 0);

    auto opened = console::open(fd, {}, loop);
    ::close(fd);
    ASSERT(opened.has_error());
    EXPECT(opened.error() == error::invalid_argument);
}

ZEST_CASE(get_winsize_reports_the_terminal_size) {
    PseudoTerminal pty;
    ASSERT(pty.terminal >= 0);
    ::winsize size{};
    size.ws_col = 80;
    size.ws_row = 24;
    ASSERT(::ioctl(pty.controller, TIOCSWINSZ, &size) == 0);
    EXPECT(guess_handle(pty.terminal) == handle_type::tty);

    auto opened = console::open(pty.terminal, {}, loop);
    ASSERT(opened.has_value());
    auto reported = opened->get_winsize();
    ASSERT(reported.has_value());
    EXPECT(reported->width == 80);
    EXPECT(reported->height == 24);
}

ZEST_CASE(write_reaches_the_terminal) {
    PseudoTerminal pty;
    ASSERT(pty.terminal >= 0);
    auto opened = console::open(pty.terminal, {}, loop);
    ASSERT(opened.has_value());
    auto print = [&]() -> task<void, error> {
        co_await opened->write(std::string_view("shown")).or_fail();
    };

    auto [printed] = run(print());
    EXPECT(printed.has_value());
    EXPECT(pty.shown() == "shown");
}

// Raw mode hands over each key as it is typed rather than each line.
ZEST_CASE(read_in_raw_mode_takes_a_key_without_a_newline) {
    PseudoTerminal pty;
    ASSERT(pty.terminal >= 0);
    auto opened = console::open(pty.terminal, console::options(true), loop);
    ASSERT(opened.has_value());
    ASSERT(!opened->set_mode(console::mode::raw));
    ASSERT(::write(pty.controller, "k", 1) == 1);
    auto type = [&]() -> task<std::string, error> {
        co_return co_await opened->read().or_fail();
    };

    auto [typed] = run(type());
    ASSERT(typed.has_value());
    EXPECT(*typed == "k");
    EXPECT(!console::reset_mode());
    EXPECT(!opened->set_mode(console::mode::normal));
}
#endif

};  // ZEST_SUITE(async_io_stream_console)

}  // namespace

}  // namespace kota
