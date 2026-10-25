#pragma once

#include <string>

#include "kota/sys/spawn.h"
#include "kota/async/io/loop.h"
#include "kota/async/io/stream.h"
#include "kota/async/runtime/task.h"
#include "kota/async/vocab/error.h"
#include "kota/async/vocab/owned.h"

namespace kota {

/// A child process, started by sys::spawn, whose exit an event loop waits
/// for: on Linux through a pidfd, or SIGCHLD where the kernel has none
/// (before 5.3), a sandbox forbids them or the loop cannot poll their epoll
/// set; on macOS through kqueue; on Windows through the process handle.
/// libuv's uv_spawn is not used: its SIGCHLD handler and its job object stay
/// out of the way, and libuv reaps only children of its own.
///
/// wait() waits for the child to exit; the exit status stays, and every
/// later wait() returns it at once. One wait() may be pending at a time; a
/// second fails with error::resource_busy_or_locked. Cancelling a wait only
/// abandons the wait: the child runs on, and a later wait() still gets its
/// exit status. To end the child, kill() it. Destroying the process ends a
/// pending wait with error::operation_aborted and leaves the child running;
/// the loop still reaps it once it exits, without waiting for it to. On
/// Windows such a child started exit_with_parent is killed when the loop
/// goes, its job closing.
///
/// A default-constructed or moved-from process is inert: what can fail fails
/// with error::invalid_argument.
class process {
public:
    using options = sys::SpawnOptions;

    using creation_options = sys::CreationOptions;

    using stdio = sys::Stdio;

    using env_var = sys::EnvVar;

    using env_change = sys::EnvChange;

    using exit_status = sys::ExitStatus;

    process() noexcept;

    process(const process&) = delete;
    process& operator=(const process&) = delete;

    process(process&& other) noexcept;
    process& operator=(process&& other) noexcept;

    ~process();

    struct spawn_result;

    /// Launches the child, with the pipes `opts` asks for. It either starts,
    /// with all of them, or leaves nothing behind. Fails with
    /// error::operation_aborted while the loop is being destroyed.
    static result<spawn_result> spawn(const options& opts,
                                      event_loop& loop = event_loop::current());

    struct capture_result;

    /// Runs the child to its exit, and returns what it wrote: its stdin
    /// reads nothing, and its stdout and stderr are read while it runs,
    /// whatever `opts` asks for them. A cancel kills the group the child
    /// leads, as kill_tree() does, and the child, as kill() does, and the
    /// task ends once the child has exited: it leaves no child behind; one
    /// after the child exited, while what it started holds its pipes, kills
    /// the group too. The child must be one the caller may signal: a cancel
    /// that cannot kill it aborts the program.
    static task<capture_result, error> capture(options opts,
                                               event_loop& loop = event_loop::current());

    /// Waits for the child to exit. Fails with error::no_such_process when
    /// another reaped it, as a waitpid(-1) elsewhere in this process does,
    /// or SIGCHLD ignored since it started.
    task<exit_status, error> wait();

    /// The OS process ID; -1 for an inert process.
    int pid() const noexcept;

    /// Sends a signal to the child; fails with no_such_process once its exit
    /// has been observed. On Windows SIGINT, SIGTERM and SIGKILL (9) end it.
    error kill(int signum);

    /// Ends the child at once, which it cannot catch: SIGKILL, or on Windows
    /// TerminateProcess. Fails as kill(signum) does.
    error kill();

    /// Sends a signal to the group the child leads (creation_options's
    /// process_group or detached), or on Windows ends its job, as
    /// sys::Child::kill_tree does; after the child's exit too, while the
    /// group has members.
    error kill_tree(int signum);

    /// Ends the group at once: SIGKILL, or on Windows TerminateJobObject.
    error kill_tree();

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

/// How a captured child ended, and all it wrote.
struct process::capture_result {
    exit_status status;

    std::string stdout_data;

    std::string stderr_data;
};

}  // namespace kota
