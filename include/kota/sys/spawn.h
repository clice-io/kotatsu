#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "kota/support/buffer_ref.h"
#include "kota/fs/file.h"
#include "kota/sys/env.h"

// Starting a child process and waiting for it, without an event loop:
// kota::process wraps this to wait for the child on one.

namespace kota::sys {

/// How a child's standard stream is set up.
struct Stdio {
    enum class Kind : std::uint8_t {
        /// This process's own stream; on Windows none where it has none
        /// open.
        Inherit,
        /// None: /dev/null, or NUL on Windows.
        Ignore,
        /// File descriptor `fd` of this process, a C runtime one on Windows.
        FD,
        /// The open file `file` refers to, which the child reads or writes
        /// from where it stands.
        File,
        /// A pipe, whose other end Child::take_pipe gives this process: the
        /// child reads stdin's and writes the others', or both, `duplex`.
        Pipe,
    };

    Kind kind = Kind::Inherit;

    int fd = -1;

    fs::File::native_handle_type file = fs::File::closed;

    bool duplex = false;

    static Stdio inherit() {
        return {};
    }

    static Stdio ignore() {
        return {.kind = Kind::Ignore};
    }

    static Stdio from_fd(int fd) {
        return {.kind = Kind::FD, .fd = fd};
    }

    /// `file` may close once the child has started.
    static Stdio from_file(const fs::File& file) {
        return {.kind = Kind::File, .file = file.native_handle()};
    }

    /// A pipe in the direction of its slot: the child reads stdin's, and
    /// writes stdout's and stderr's.
    static Stdio pipe() {
        return {.kind = Kind::Pipe};
    }

    /// A pipe both read and written, by the child and by this process.
    static Stdio duplex_pipe() {
        return {.kind = Kind::Pipe, .duplex = true};
    }
};

/// A change to a child's environment: sets variable `name` to `value`, or
/// removes it when there is no value.
struct EnvChange {
    std::string name;

    std::optional<std::string> value = {};
};

struct CreationOptions {
    /// A session of its own (setsid), out of reach of this process's
    /// terminal; on Windows a console of its own without a window, which the
    /// console programs it starts share, and a process group of its own. It
    /// leads a process group as process_group's does.
    bool detached = false;

    /// The child leads a group of its own, which kill_tree ends as a whole.
    /// On POSIX a process group (setpgid), in the background of the terminal
    /// this process has, if any: Ctrl+C and a hangup do not reach it, and
    /// reading the terminal stops it (SIGTTIN). The processes it starts stay
    /// in the group unless they leave it, as a shell's jobs and ninja's
    /// commands do. On Windows a new console process group, which Ctrl+C
    /// does not reach, and a job object its descendants stay in unless they
    /// ask to break away from it.
    bool process_group = false;

    /// The child is killed when this process exits. On Linux by
    /// PR_SET_PDEATHSIG, set before it runs: the child alone, and when the
    /// thread that started it exits, which may be before this process does
    /// (cleared too when it runs a set-user-ID program). On Windows its job
    /// is killed when closed: the whole tree with process_group or detached,
    /// else the child alone; the job closes once this process exits, or its
    /// Child is destroyed. not_supported on macOS, which has nothing to do it
    /// with.
    bool exit_with_parent = false;

    /// Windows: no console window, and none for the console programs it
    /// starts, when no stream is this process's. Ignored elsewhere, as the
    /// ones below are.
    bool windows_hide_console = false;

    /// Windows: the window of a GUI program hidden.
    bool windows_hide_gui = false;

    /// Windows: every inheritable handle of this process's reaches the
    /// child, as CreateProcess with bInheritHandles passes them, rather than
    /// its standard handles alone: for a program that starts another in its
    /// place and passes on the handles it was given. An fs::File's is not
    /// one until SetHandleInformation makes it so. One made inheritable on
    /// another thread meanwhile reaches it too; the standard handles that
    /// spawn makes for other children never do, every other spawn waiting
    /// while this child is created. On POSIX every descriptor without
    /// close-on-exec reaches the child anyway.
    bool windows_inherit_handles = false;
};

struct SpawnOptions {
    /// The program: a path, relative to `cwd` when it is relative and `cwd`
    /// is given, when it holds a separator; else looked for in the child's
    /// PATH, a relative entry relative to `cwd` too. On POSIX by
    /// find_program's rules, PATH unset searching confstr(_CS_PATH)'s
    /// directories, and a file without a #! line run by /bin/sh, as execvp
    /// does; on Windows by find_create_process_program's, in PATH alone, not,
    /// as CreateProcess, in this program's directory and the current one
    /// first; PATH unset searching the system's directories. A `.bat` or
    /// `.cmd` file runs only with a windows_command_line, which says how cmd
    /// is to read its arguments.
    std::string program = {};

    /// The arguments after argv[0].
    std::vector<std::string> args = {};

    /// argv[0], which need not be `program`; `program` when unset. On
    /// Windows it holds no quote, which a command line has no way to write
    /// for it.
    std::optional<std::string> arg0 = {};

    /// The child's environment; unset, this process's. Given, it is the
    /// child's as it is, in its order, on Windows too: nothing is added,
    /// though some programs fail without SYSTEMROOT.
    std::optional<std::vector<EnvVar>> env = {};

    /// Changes made, in order, to `env`, or to this process's; the last of a
    /// name counts. Names compare as env_name_equal compares them.
    std::vector<EnvChange> env_changes = {};

    /// The child's working directory; empty, this process's.
    std::string cwd = {};

    CreationOptions creation = {};

    /// stdin, stdout and stderr.
    std::array<Stdio, 3> streams = {Stdio::inherit(), Stdio::inherit(), Stdio::inherit()};

    /// Windows: the command line, as it is, instead of one written from
    /// `arg0` and `args`, which are not read then. Ignored elsewhere.
    std::string windows_command_line = {};

    /// Windows: called with the child's process and main thread handles
    /// once it is created, before that thread runs a single instruction,
    /// in the job it is in; an error kills it and is spawn's. Ignored
    /// elsewhere.
    std::function<std::error_code(void* process, void* thread)> windows_before_start = {};
};

/// How a child ended.
struct ExitStatus {
    /// Its exit code: on POSIX 0 for one a signal ended, which `term_signal`
    /// says; on Windows 1 for one kill or kill_tree ended, and for one that
    /// crashed the NTSTATUS of its exception, such as 0xC0000005, unsigned.
    std::int64_t code = 0;

    /// The signal that ended it, 0 if none. On Windows the signal its kill
    /// or kill_tree sent, if it ended the child.
    int term_signal = 0;

    /// CPU time, in user mode and in the kernel: of the child and the
    /// descendants it waited for, on POSIX; on Windows of every process
    /// that ran in its job with process_group or detached, else of the
    /// child alone.
    std::chrono::microseconds user_time{};

    std::chrono::microseconds system_time{};

    /// Whether the child exited with code 0, rather than failing, crashing
    /// or being killed.
    bool success() const noexcept {
        return code == 0 && term_signal == 0;
    }

    /// How the child ended, for a person: "exit code 1", "signal 9
    /// (SIGKILL)", or on Windows "exception 0xC0000005
    /// (STATUS_ACCESS_VIOLATION)".
    std::string to_string() const;

    friend bool operator==(const ExitStatus&, const ExitStatus&) = default;
};

/// A child process, which this process alone waits for. Destroying it
/// leaves the child running; on POSIX a child not waited for stays a zombie
/// until this process exits, and on Windows one started exit_with_parent is
/// killed then, its job closing.
///
/// A default-constructed or moved-from Child is inert: what can fail fails
/// with invalid_argument.
class Child {
public:
#ifdef _WIN32
    using native_handle_type = void*;
#else
    using native_handle_type = int;
#endif

    Child() noexcept = default;

    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;

    Child(Child&& other) noexcept;
    Child& operator=(Child&& other) noexcept;

    ~Child();

    /// Its process ID; -1 for an inert Child.
    int pid() const noexcept {
        return id;
    }

    /// What its exit is waited on with: on Windows its process handle; on
    /// Linux a pidfd, readable once it has exited, or -1 on a kernel without
    /// them (before 5.3) or where a sandbox forbids them; -1 elsewhere. It
    /// stays open, after the child is reaped too, until the Child goes.
    native_handle_type native_handle() const noexcept {
        return handle;
    }

    /// The end this process keeps of stream `index` (0 to 2) when it is a
    /// pipe, given away: the caller closes it. On POSIX a pipe, or a socket
    /// pair for one both read and written; on Windows a named pipe opened
    /// for overlapped I/O, which an event loop takes. Closed, or given away
    /// already, otherwise.
    fs::File::native_handle_type take_pipe(std::size_t index) noexcept;

    /// Waits for the child to exit, and reaps it. Once it has been reaped,
    /// the status it ended with. no_child_process when another reaped it, as
    /// SIGCHLD set to SIG_IGN makes the system do.
    std::expected<ExitStatus, std::error_code> wait();

    /// The status it ended with when it has exited, reaping it; unset while
    /// it runs. Fails as wait() does.
    std::expected<std::optional<ExitStatus>, std::error_code> try_wait();

    /// Sends signal `signum` to the child, through its pidfd where it has
    /// one: never to another process that took its pid after another reaped
    /// it. On Windows SIGINT, SIGTERM and SIGKILL (9) end it, and 0 tells
    /// whether it runs. no_such_process once it has been reaped, or on
    /// Windows once it has exited.
    std::error_code kill(int signum);

    /// Ends the child at once, which it cannot catch: SIGKILL, or on Windows
    /// TerminateProcess.
    std::error_code kill();

    /// Sends signal `signum` to the group it leads (killpg), or on Windows
    /// ends its job (TerminateJobObject, which returns before they are all
    /// gone); after it has exited too, while the group has members. On POSIX
    /// the ID of a group left empty may be another's once the child is
    /// reaped: kill_tree then is for a group known to have members.
    /// invalid_argument for a child started without process_group or
    /// detached; no_such_process for a group with no members left.
    std::error_code kill_tree(int signum);

    /// Ends the group at once: SIGKILL, or on Windows TerminateJobObject.
    std::error_code kill_tree();

private:
    friend std::expected<Child, std::error_code> spawn(const SpawnOptions& options);

    void swap(Child& other) noexcept;

    /// Reaps the child into `exited` once it has exited, waiting for that
    /// when `block`; nothing once it has been reaped.
    std::error_code reap(bool block);

    int id = -1;

    native_handle_type handle = invalid_handle;

    /// The pipe ends of streams 0 to 2.
    std::array<fs::File::native_handle_type, 3> pipes = {fs::File::closed,
                                                         fs::File::closed,
                                                         fs::File::closed};

    /// How it ended, once it has been reaped.
    std::optional<ExitStatus> exited;

    /// It leads a process group, or a job its descendants stay in.
    bool leads_group = false;

#ifdef _WIN32
    static constexpr native_handle_type invalid_handle = nullptr;

    void* job = nullptr;

    /// The signal kill or kill_tree last ended it with.
    int killed_by = 0;
#else
    static constexpr native_handle_type invalid_handle = -1;

    /// Another reaped it, as SIGCHLD set to SIG_IGN makes the system do.
    bool lost = false;
#endif
};

/// Starts a child as `options` say. On POSIX its signal handlers are reset
/// and its signal mask cleared; the descriptors this process has without
/// close-on-exec reach it, as make's jobserver needs. On Windows it inherits
/// its three standard handles alone, unless windows_inherit_handles, and is
/// born in a job object of its own, nested in any this process is in, which
/// ends it rather than show a dialog when it crashes.
/// no_such_file_or_directory or permission_denied for a program not found,
/// as find_program; invalid_argument for a negative descriptor, a closed
/// file, a batch file without windows_command_line or, on Windows, an
/// argv[0] with a quote; bad_file_descriptor for a descriptor not open;
/// not_supported for exit_with_parent on macOS.
std::expected<Child, std::error_code> spawn(const SpawnOptions& options);

/// Runs a child to its end, as `options` say but for its streams, whatever
/// they ask: its stdin reads nothing, and what its stdout and its stderr
/// write is read into `out` and `err` while it runs, as the bytes they are,
/// until both end, which a process it started holding them puts off. The
/// two may be one buffer, which takes what each read gives in turn. How the
/// child ended. Fails as spawn() does, or with the error of a read, the
/// child then killed, and its group first when it leads one, and reaped.
std::expected<ExitStatus, std::error_code> capture(buffer_ref out,
                                                   buffer_ref err,
                                                   SpawnOptions options);

}  // namespace kota::sys
