#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "error.h"
#include "owned.h"
#include "stream.h"
#include "task.h"

namespace eventide {

class event_loop;

class process {
public:
    process() noexcept;

    process(const process&) = delete;
    process& operator=(const process&) = delete;

    process(process&& other) noexcept;
    process& operator=(process&& other) noexcept;

    ~process();

    struct Self;
    Self* operator->() noexcept;

    struct exit_status {
        /// Exit code reported by the child.
        int64_t status;

        /// Terminating signal number if signalled, 0 otherwise.
        int term_signal;
    };

    struct stdio {
        enum class kind {
            inherit,  // inherit parent's stdio
            ignore,   // discard this stream
            fd,       // inherit a specific file descriptor
            pipe      // create a pipe
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

    using wait_result = result<exit_status>;

    /// Launch the process; creates pipes as requested in options.
    struct spawn_result;

    /// Spawn a child process within the given loop.
    static result<spawn_result> spawn(const options& opts,
                                      event_loop& loop = event_loop::current());

    /// Await process termination and fetch exit status.
    task<wait_result> wait();

    /// Retrieve OS pid for the process; -1 if not started.
    int pid() const noexcept;

    /// Send a signal to the process.
    error kill(int signum);

private:
    explicit process(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

struct process::spawn_result {
    process proc;

    pipe stdin_pipe;

    pipe stdout_pipe;

    pipe stderr_pipe;
};

}  // namespace eventide
