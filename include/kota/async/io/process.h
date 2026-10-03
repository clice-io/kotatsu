#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "kota/async/io/loop.h"
#include "kota/async/io/stream.h"
#include "kota/async/runtime/task.h"
#include "kota/async/vocab/error.h"
#include "kota/async/vocab/owned.h"

namespace kota {

/// A child process.
///
/// wait() waits for the child to exit; the exit status stays, and every
/// later wait() returns it at once. One wait() may be pending at a time; a
/// second fails with error::resource_busy_or_locked. Cancelling a wait only
/// abandons the wait: the child runs on, and a later wait() still gets its
/// exit status. To end the child, kill() it. Destroying the process ends a
/// pending wait with error::operation_aborted and leaves the child running.
///
/// A default-constructed or moved-from process is inert: what can fail fails
/// with error::invalid_argument.
class process {
public:
    process() noexcept;

    process(const process&) = delete;
    process& operator=(const process&) = delete;

    process(process&& other) noexcept;
    process& operator=(process&& other) noexcept;

    ~process();

    struct exit_status {
        /// Exit code reported by the child.
        int64_t status;

        /// Terminating signal number if signalled, 0 otherwise.
        int term_signal;

        /// Whether the child exited with code 0 rather than by a signal.
        bool success() const noexcept {
            return status == 0 && term_signal == 0;
        }

        /// How the child ended, for people: "exit code 3", or "signal 9" with
        /// the signal's name where the system has one. On Windows a crash
        /// ends the child with an NTSTATUS code, given in hex and named when
        /// it is a common one: "exit code 0xC0000005 (access violation)".
        std::string to_string() const;
    };

    struct stdio {
        enum class kind : std::uint8_t {
            /// Inherit the parent's stream.
            inherit,
            /// Discard the stream.
            ignore,
            /// Inherit a given file descriptor.
            fd,
            /// Create a pipe.
            pipe,
        };

        /// How this stream should be configured for the child.
        kind type = kind::inherit;

        /// Descriptor to inherit when type == fd.
        int descriptor = -1;

        /// Child-readable flag when type == pipe.
        bool readable = false;  // from the child's perspective

        /// Child-writable flag when type == pipe.
        bool writable = false;  // from the child's perspective

        /// Inherit parent's descriptor (default).
        static stdio inherit();

        /// Discard this stream for the child.
        static stdio ignore();

        /// Duplicate the given descriptor into the child.
        static stdio from_fd(int fd);

        /// Create a pipe; flags are from the child's perspective.
        static stdio pipe(bool readable, bool writable);
    };

    struct creation_options {
        /// Detach the child from the parent process group/session.
        bool detached = false;

        /// Hide the console window (Windows).
        bool windows_hide = false;

        /// Hide the console window specifically (Windows).
        bool windows_hide_console = false;

        /// Hide GUI window (Windows).
        bool windows_hide_gui = false;

        /// Disable argument quoting/escaping (Windows).
        bool windows_verbatim_arguments = false;

        /// Use exact file path for image name (Windows).
        bool windows_file_path_exact_name = false;
    };

    struct options {
        /// Executable path.
        std::string file;

        /// argv (including argv[0]). If empty, defaults to `file`.
        std::vector<std::string> args;

        /// Environment variables in `KEY=VALUE` form; empty means inherit.
        std::vector<std::string> env;

        /// Working directory; empty means inherit.
        std::string cwd;

        /// Process creation options (platform-specific options may be ignored).
        creation_options creation;

        /// Stdio config for stdin/stdout/stderr.
        std::array<stdio, 3> streams = {stdio::inherit(), stdio::inherit(), stdio::inherit()};
    };

    struct spawn_result;

    /// Launches the child, with the pipes `opts` asks for.
    static result<spawn_result> spawn(const options& opts,
                                      event_loop& loop = event_loop::current());

    struct capture_result;

    /// Runs the child `opts` describes to its end, with its stdout and stderr
    /// piped whatever `opts.streams` says for them, and gives how it ended and
    /// all it wrote to each. The pipes are read while the child runs, so a
    /// child that fills one does not stall. A read that fails, or a cancel,
    /// leaves the child running, as destroying a process does.
    static task<capture_result, error> capture(options opts,
                                               event_loop& loop = event_loop::current());

    /// Waits for the child to exit.
    task<exit_status, error> wait();

    /// The OS process id; -1 for an inert process.
    int pid() const noexcept;

    /// Sends a signal to the child; fails with no_such_process once its exit
    /// has been observed.
    error kill(int signum);

    /// Ends the child at once: SIGKILL on POSIX, TerminateProcess on Windows,
    /// whose exit status libuv reports with SIGKILL as well. Fails as
    /// kill(signum) does.
    error kill();

private:
    struct Self;

    explicit process(detail::unique_handle<Self> self) noexcept;

    detail::unique_handle<Self> self;
};

/// A launched child, with the parent's end of each pipe it asked for; the
/// other pipes are inert.
struct process::spawn_result {
    process proc;

    pipe stdin_pipe;

    pipe stdout_pipe;

    pipe stderr_pipe;
};

/// How a child that process::capture() ran ended, and what it wrote.
struct process::capture_result {
    exit_status status;

    std::string stdout_text;

    std::string stderr_text;
};

}  // namespace kota
