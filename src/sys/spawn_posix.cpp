#ifndef _WIN32

#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <format>
#include <string>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "child.h"
#include "os.h"
#include "signals.h"
#include "kota/sys/spawn.h"

#ifdef __APPLE__
#include <mutex>
#include <spawn.h>
#include <sys/event.h>
#else
#include <sys/prctl.h>
#include <sys/syscall.h>

// The numbers of pidfd_send_signal and pidfd_open on every architecture but
// Alpha's and MIPS's, for C libraries whose headers are older than them
// (Linux 5.1 and 5.3).
#if !defined(__alpha__) && !defined(__mips__)
#ifndef SYS_pidfd_send_signal
#define SYS_pidfd_send_signal 424
#endif
#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#endif
#endif

namespace kota::sys {

namespace {

/// A descriptor, closed when this goes.
struct Descriptor {
    int fd = -1;

    Descriptor() = default;

    explicit Descriptor(int fd) noexcept : fd(fd) {}

    Descriptor(Descriptor&& other) noexcept : fd(std::exchange(other.fd, -1)) {}

    Descriptor& operator=(Descriptor&& other) noexcept {
        std::swap(fd, other.fd);
        return *this;
    }

    ~Descriptor() {
        if(fd >= 0) {
            ::close(fd);
        }
    }

    int release() noexcept {
        return std::exchange(fd, -1);
    }
};

/// Two connected descriptors, close-on-exec: a pipe, `[0]` read and `[1]`
/// written, or a socket pair.
std::expected<std::array<Descriptor, 2>, std::error_code> connected_pair(bool socket) {
    int fds[2];
#ifdef __APPLE__
    // Neither pipe2 nor SOCK_CLOEXEC on macOS: the flag goes on after.
    int made = socket ? ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) : ::pipe(fds);
    if(made == 0) {
        ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    }
#else
    int made = socket ? ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds)
                      : ::pipe2(fds, O_CLOEXEC);
#endif
    if(made != 0) {
        return std::unexpected(errno_code());
    }
    std::array<Descriptor, 2> pair = {Descriptor(fds[0]), Descriptor(fds[1])};
    for(auto& end: pair) {
        // Above the standard streams, which the child's are put in place of:
        // in a process that closed one, a new descriptor takes its number.
        if(end.fd < 3) {
            Descriptor moved(::fcntl(end.fd, F_DUPFD_CLOEXEC, 3));
            if(moved.fd < 0) {
                return std::unexpected(errno_code());
            }
            end = std::move(moved);
        }
    }
    if(socket) {
        // What libuv gives a child's socket pair, macOS's own being 8 KiB.
        int size = 64 * 1024;
        for(const auto& end: pair) {
            ::setsockopt(end.fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
            ::setsockopt(end.fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
        }
    }
    return pair;
}

/// What a stream's descriptor is for one that is /dev/null, and for one that
/// is closed in the child as it is here.
constexpr int dev_null = -1;
constexpr int closed_stream = -2;

/// How the child ended, from wait4.
ExitStatus exit_status_of(int status, const rusage& usage) {
    auto microseconds = [](const timeval& time) {
        return std::chrono::seconds(time.tv_sec) + std::chrono::microseconds(time.tv_usec);
    };
    return ExitStatus{
        .code = WIFEXITED(status) ? WEXITSTATUS(status) : 0,
        .term_signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0,
        .user_time = microseconds(usage.ru_utime),
        .system_time = microseconds(usage.ru_stime),
    };
}

#ifndef __APPLE__

/// Writes errno to `error_fd` and exits, from the child.
[[noreturn]] void fail_in_child(int error_fd) {
    int code = errno;
    while(::write(error_fd, &code, sizeof(code)) < 0 && errno == EINTR) {}
    ::_exit(127);
}

/// What the child does between fork and exec: only async-signal-safe calls,
/// on what the parent prepared.
struct ChildSetup {
    const char* path;
    char* const* argv;
    /// /bin/sh, `path` and the arguments after argv[0]: what runs a file
    /// that is no program the kernel knows, as execvp runs it.
    char* const* shell_argv;
    char* const* envp;
    /// The descriptor each stream takes, or dev_null or closed_stream.
    std::array<int, 3> fds;
    const char* cwd;
    bool detached;
    bool process_group;
    bool exit_with_parent;
    pid_t parent;
    int error_fd;
    /// SIGRTMIN and SIGRTMAX, which glibc reads through calls of its own.
    int rtmin;
    int rtmax;
    /// The signals as exec leaves them, rather than all at their defaults.
    bool keep_signals;
    /// The signal mask it starts with.
    sigset_t mask;

    /// The action exec leaves signal `signum` with: the default for one
    /// with a handler, which means nothing to another program, an ignored
    /// one ignored; one kotatsu changed as it was. What kotatsu changed is
    /// read here, after the fork, which took it with the actions it goes
    /// with.
    void (*exec_action(int signum) const)(int) {
        struct sigaction action;
        ::sigaction(signum, nullptr, &action);
        if(signum == SIGPIPE && sigpipe_was_default) {
            return SIG_DFL;
        }
        if(signum == SIGCHLD && sigchld_was_ignored && action.sa_handler == SIG_DFL) {
            return SIG_IGN;
        }
        return action.sa_handler == SIG_IGN ? SIG_IGN : SIG_DFL;
    }

    [[noreturn]] void run() {
        // Before the mask lets a signal in: a handler of this process's
        // would run here. Those between 31 and SIGRTMIN are glibc's own.
        for(int signum = 1; signum <= rtmax; signum = signum == 31 ? rtmin : signum + 1) {
            if(signum == SIGKILL || signum == SIGSTOP) {
                continue;
            }
            if(::signal(signum, keep_signals ? exec_action(signum) : SIG_DFL) == SIG_ERR) {
                fail_in_child(error_fd);
            }
        }
        if(detached ? ::setsid() < 0 : process_group && ::setpgid(0, 0) < 0) {
            fail_in_child(error_fd);
        }
        if(exit_with_parent) {
            if(::prctl(PR_SET_PDEATHSIG, SIGKILL) < 0) {
                fail_in_child(error_fd);
            }
            // The parent died before the signal was armed: none will come.
            // A thread of it that exited would not show here, but the one
            // that forked waits until the exec.
            if(::getppid() != parent) {
                ::kill(::getpid(), SIGKILL);
            }
        }
        // Before the streams change, which a working directory not there
        // would otherwise have changed for nothing.
        if(cwd != nullptr && ::chdir(cwd) < 0) {
            fail_in_child(error_fd);
        }
        // A descriptor below the stream it goes to may be a stream's
        // target before its turn (stdout and stderr swapped): it moves above
        // them first.
        for(int target = 0; target < 3; ++target) {
            if(fds[target] >= 0 && fds[target] < target) {
                fds[target] = ::fcntl(fds[target], F_DUPFD_CLOEXEC, 3);
                if(fds[target] < 0) {
                    fail_in_child(error_fd);
                }
            }
        }
        for(int target = 0; target < 3; ++target) {
            int fd = fds[target];
            if(fd == closed_stream) {
                ::close(target);
                continue;
            }
            if(fd == dev_null) {
                ::close(target);
                fd = ::open("/dev/null", target == 0 ? O_RDONLY : O_RDWR);
                if(fd < 0) {
                    fail_in_child(error_fd);
                }
                if(fd != target && (::dup2(fd, target) < 0 || ::close(fd) < 0)) {
                    fail_in_child(error_fd);
                }
                continue;
            }
            if(fd == target ? ::fcntl(fd, F_SETFD, 0) < 0 : ::dup2(fd, target) < 0) {
                fail_in_child(error_fd);
            }
        }
        // Shared with whoever else has it open, as libuv does: a child given
        // a non-blocking stream would see EAGAIN. Last, so that only the exec
        // fails after, and gives the flags back: the parent shares them.
        std::array<int, 3> nonblocking = {-1, -1, -1};
        for(int target = 0; target < 3; ++target) {
            int flags = fds[target] < 0 ? -1 : ::fcntl(target, F_GETFL);
            if(flags >= 0 && (flags & O_NONBLOCK)) {
                ::fcntl(target, F_SETFL, flags & ~O_NONBLOCK);
                nonblocking[target] = flags;
            }
        }
        ::sigprocmask(SIG_SETMASK, &mask, nullptr);
        // libc's execve, which a hook preloaded into this process sees.
        ::execve(path, argv, envp);
        if(errno == ENOEXEC) {
            ::execve(shell_argv[0], shell_argv, envp);
        }
        const int exec_error = errno;
        for(int target = 0; target < 3; ++target) {
            if(nonblocking[target] >= 0) {
                ::fcntl(target, F_SETFL, nonblocking[target]);
            }
        }
        errno = exec_error;
        fail_in_child(error_fd);
    }
};

/// A pidfd of child `pid`, or -1 where the kernel has none or a sandbox
/// forbids it.
int open_pidfd([[maybe_unused]] pid_t pid) {
#ifdef SYS_pidfd_open
    int fd = static_cast<int>(::syscall(SYS_pidfd_open, pid, 0));
    // Above the standard streams, as connected_pair's ends: in a process
    // that closed one it would take its number, and reach a child that
    // inherits the stream.
    if(fd >= 0 && fd < 3) {
        int moved = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
        ::close(fd);
        fd = moved;
    }
    return fd;
#else
    return -1;
#endif
}

#else

/// Held while a pipe is made and until posix_spawn has started the child it
/// is for: macOS sets close-on-exec only after it makes one, which another
/// of these spawns would otherwise give its child.
std::mutex spawning;

/// Waits for process `pid` to exit, through kqueue: a wait4 that blocks
/// waits for good on macOS for a child the system reaps itself, SIGCHLD
/// ignored. A zombie, or a process exiting, is none kqueue knows of.
std::error_code wait_for_exit(pid_t pid) {
    Descriptor queue(::kqueue());
    if(queue.fd < 0) {
        return errno_code();
    }
    struct kevent change;
    EV_SET(&change,
           static_cast<uintptr_t>(pid),
           EVFILT_PROC,
           EV_ADD | EV_ONESHOT,
           NOTE_EXIT,
           0,
           nullptr);
    struct kevent event;
    while(true) {
        int got = ::kevent(queue.fd, &change, 1, &event, 1, nullptr);
        if(got < 0 && errno == EINTR) {
            continue;
        }
        if(got < 0) {
            return errno == ESRCH ? std::error_code() : errno_code();
        }
        if((event.flags & EV_ERROR) && event.data != ESRCH) {
            return {static_cast<int>(event.data), std::generic_category()};
        }
        return {};
    }
}

#endif

}  // namespace

Child::~Child() {
    if(handle >= 0) {
        ::close(handle);
    }
    for(int fd: pipes) {
        if(fd >= 0) {
            ::close(fd);
        }
    }
}

std::error_code Child::reap(bool block) {
    if(exited) {
        return {};
    }
#ifdef __APPLE__
    // Once it has exited, wait4 need not block but for the moment the child
    // takes to become a zombie.
    if(block) {
        if(auto error = wait_for_exit(id)) {
            return error;
        }
    }
    const int options = WNOHANG;
#else
    const int options = block ? 0 : WNOHANG;
#endif
    while(true) {
        int status = 0;
        rusage usage{};
        pid_t reaped = ::wait4(id, &status, options, &usage);
        if(reaped < 0 && errno == EINTR) {
            continue;
        }
        if(reaped < 0) {
            lost = errno == ECHILD;
            return errno_code();
        }
        // A traced child's stop, which wait4 reports to its tracer, this
        // process, anyway, ends nothing; it is taken all the same, a program
        // that traces its own children waiting for them itself.
        if(reaped > 0 && !WIFSTOPPED(status)) {
            exited = exit_status_of(status, usage);
            return {};
        }
        if(!block) {
            return {};
        }
        std::this_thread::yield();
    }
}

std::error_code Child::kill(int signum) {
    if(id < 0) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    // Once it is reaped, by this process or another, its pid may be
    // another process's.
    if(exited || lost) {
        return std::make_error_code(std::errc::no_such_process);
    }
#ifdef SYS_pidfd_send_signal
    if(handle >= 0) {
        if(::syscall(SYS_pidfd_send_signal, handle, signum, nullptr, 0) == 0) {
            return {};
        }
        if(errno != ENOSYS) {
            return errno_code();
        }
    }
#endif
    // By its pid: while it is this process's child, a zombie too, the pid
    // is no other process's. One another reaped is not.
    siginfo_t info{};
    if(::waitid(P_PID, static_cast<id_t>(id), &info, WEXITED | WNOHANG | WNOWAIT) < 0 &&
       errno == ECHILD) {
        lost = true;
        return std::make_error_code(std::errc::no_such_process);
    }
    if(::kill(id, signum) != 0) {
        return errno_code();
    }
    return {};
}

std::error_code Child::kill_tree(int signum) {
    if(id < 0 || !leads_group) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    // While the group has members, its ID is no other process's.
    if(::killpg(id, signum) != 0) {
        return errno_code();
    }
    return {};
}

std::expected<Child, std::error_code> spawn(const SpawnOptions& options) {
    if(holds_nul(options)) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    const auto& creation = options.creation;
#ifdef __APPLE__
    if(creation.exit_with_parent) {
        return std::unexpected(std::make_error_code(std::errc::not_supported));
    }
#endif
    // A copy of this process's own too: setenv in another thread may move
    // its array while the child is made.
    auto environment = child_environment(options);
    if(!environment) {
        environment = sys::environment();
    }
    auto program = child_program(options, environment);
    if(!program) {
        return std::unexpected(program.error());
    }
    // Each descriptor given is looked at before a pipe is made: one not open
    // would otherwise be the number a pipe takes.
    for(const auto& stream: options.streams) {
        if(stream.kind == Stdio::Kind::FD || stream.kind == Stdio::Kind::File) {
            int fd = stream.kind == Stdio::Kind::FD ? stream.fd : stream.file;
            if(fd < 0) {
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
            }
            if(::fcntl(fd, F_GETFD) < 0) {
                return std::unexpected(errno_code());
            }
        }
    }

    // What the child gets, laid out before it starts.
    auto args = child_arguments(options);
    std::vector<char*> argv;
    for(auto& arg: args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    std::string shell = "/bin/sh";
    std::vector<char*> shell_argv = {shell.data(), program->data()};
    shell_argv.insert(shell_argv.end(), argv.begin() + 1, argv.end());
    std::vector<std::string> variables;
    std::vector<char*> envp;
    for(const auto& [name, value]: *environment) {
        variables.push_back(std::format("{}={}", name, value));
    }
    for(auto& variable: variables) {
        envp.push_back(variable.data());
    }
    envp.push_back(nullptr);
    char* const* child_envp = envp.data();
#ifdef __APPLE__
    std::lock_guard lock(spawning);
#endif

    Child child;
    // The child's end of each pipe, closed here once it has started.
    std::array<Descriptor, 3> child_ends;
    std::array<int, 3> fds = {dev_null, dev_null, dev_null};
    for(std::size_t i = 0; i < 3; ++i) {
        const auto& stream = options.streams[i];
        switch(stream.kind) {
            // One this process has closed it has closed too.
            case Stdio::Kind::Inherit:
                fds[i] =
                    ::fcntl(static_cast<int>(i), F_GETFD) < 0 ? closed_stream : static_cast<int>(i);
                break;
            case Stdio::Kind::Ignore: break;
            case Stdio::Kind::FD: fds[i] = stream.fd; break;
            case Stdio::Kind::File: fds[i] = stream.file; break;
            case Stdio::Kind::Pipe: {
                auto pair = connected_pair(stream.duplex);
                if(!pair) {
                    return std::unexpected(pair.error());
                }
                // A pipe the child reads, stdin's, it reads from [0].
                const std::size_t ours = stream.duplex || i == 0 ? 1 : 0;
                child.pipes[i] = (*pair)[ours].release();
                child_ends[i] = std::move((*pair)[1 - ours]);
                fds[i] = child_ends[i].fd;
                break;
            }
        }
    }
    child.leads_group = creation.detached || creation.process_group;
    // Before the child exists.
    stop_auto_reaping();

#ifdef __APPLE__
    // posix_spawn, a system call of its own on macOS: no fork of this
    // process, however large, and nothing of it runs in the child.
    struct Setup {
        posix_spawnattr_t attributes;
        posix_spawn_file_actions_t actions;
        int attributes_error = posix_spawnattr_init(&attributes);
        int actions_error = posix_spawn_file_actions_init(&actions);

        ~Setup() {
            if(actions_error == 0) {
                posix_spawn_file_actions_destroy(&actions);
            }
            if(attributes_error == 0) {
                posix_spawnattr_destroy(&attributes);
            }
        }
    } setup;

    if(int error = setup.attributes_error != 0 ? setup.attributes_error : setup.actions_error) {
        return std::unexpected(std::error_code(error, std::generic_category()));
    }
    auto& attributes = setup.attributes;
    auto& actions = setup.actions;
    // The first error of the calls that set it up, a path too long among
    // them, which the spawn fails with.
    int setup_error = 0;
    auto check = [&](int result) {
        if(setup_error == 0) {
            setup_error = result;
        }
    };
    // A session's leader leads its group too; macOS runs SETPGROUP before
    // SETSID, so never both.
    short flags = 0;
    if(creation.detached) {
        flags |= POSIX_SPAWN_SETSID;
    } else if(creation.process_group) {
        flags |= POSIX_SPAWN_SETPGROUP;
    }
    sigset_t signals;
    if(!creation.keep_signals) {
        // Every signal at its default, none blocked.
        flags |= POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK;
        sigfillset(&signals);
        check(posix_spawnattr_setsigdefault(&attributes, &signals));
        sigemptyset(&signals);
        check(posix_spawnattr_setsigmask(&attributes, &signals));
    } else if(sigpipe_was_default) {
        // As exec leaves them, which posix_spawn does, but for the SIGPIPE
        // kotatsu ignored.
        flags |= POSIX_SPAWN_SETSIGDEF;
        sigemptyset(&signals);
        sigaddset(&signals, SIGPIPE);
        check(posix_spawnattr_setsigdefault(&attributes, &signals));
    }
    check(posix_spawnattr_setflags(&attributes, flags));
    check(posix_spawnattr_setpgroup(&attributes, 0));
    if(!options.cwd.empty()) {
        check(posix_spawn_file_actions_addchdir_np(&actions, options.cwd.c_str()));
    }
    // Each stream's descriptor, where it is not the stream itself, goes in
    // from a copy of its own: a number free here, so free in the child but
    // for the copy, which no stream takes and close-on-exec closes.
    std::array<Descriptor, 3> copies;
    for(int target = 0; target < 3; ++target) {
        int fd = fds[target];
        if(fd == dev_null) {
            check(posix_spawn_file_actions_addopen(&actions,
                                                   target,
                                                   "/dev/null",
                                                   target == 0 ? O_RDONLY : O_RDWR,
                                                   0));
        } else if(fd == target) {
            check(posix_spawn_file_actions_addinherit_np(&actions, fd));
        } else if(fd == closed_stream) {
            // Closed whatever another thread opened there meanwhile: open
            // first, as a close of what is not open fails the spawn.
            check(posix_spawn_file_actions_addopen(&actions, target, "/dev/null", O_RDONLY, 0));
            check(posix_spawn_file_actions_addclose(&actions, target));
        } else {
            copies[target] = Descriptor(::fcntl(fd, F_DUPFD_CLOEXEC, 3));
            if(copies[target].fd < 0) {
                return std::unexpected(errno_code());
            }
            check(posix_spawn_file_actions_adddup2(&actions, copies[target].fd, target));
        }
    }
    if(setup_error != 0) {
        return std::unexpected(std::error_code(setup_error, std::generic_category()));
    }
    // Shared with whoever else has it open, as libuv does; put back if the
    // spawn fails.
    std::array<int, 3> nonblocking = {-1, -1, -1};
    for(int target = 0; target < 3; ++target) {
        if(fds[target] < 0) {
            continue;
        }
        int status = ::fcntl(fds[target], F_GETFL);
        if(status >= 0 && (status & O_NONBLOCK)) {
            ::fcntl(fds[target], F_SETFL, status & ~O_NONBLOCK);
            nonblocking[target] = status;
        }
    }
    pid_t pid = -1;
    int failed = 0;
    do {
        failed =
            ::posix_spawn(&pid, program->c_str(), &actions, &attributes, argv.data(), child_envp);
        // A file that is no program the kernel knows, which execvp runs.
        if(failed == ENOEXEC) {
            failed = ::posix_spawn(&pid,
                                   shell_argv[0],
                                   &actions,
                                   &attributes,
                                   shell_argv.data(),
                                   child_envp);
        }
    } while(failed == EINTR);
    if(failed != 0) {
        for(int target = 0; target < 3; ++target) {
            if(nonblocking[target] >= 0) {
                ::fcntl(fds[target], F_SETFL, nonblocking[target]);
            }
        }
        return std::unexpected(std::error_code(failed, std::generic_category()));
    }
    child.id = pid;
#else
    auto report = connected_pair(false);
    if(!report) {
        return std::unexpected(report.error());
    }
    ChildSetup setup{
        .path = program->c_str(),
        .argv = argv.data(),
        .shell_argv = shell_argv.data(),
        .envp = child_envp,
        .fds = fds,
        .cwd = options.cwd.empty() ? nullptr : options.cwd.c_str(),
        .detached = creation.detached,
        .process_group = creation.process_group,
        .exit_with_parent = creation.exit_with_parent,
        .parent = ::getpid(),
        .error_fd = (*report)[1].fd,
        .rtmin = SIGRTMIN,
        .rtmax = SIGRTMAX,
        .keep_signals = creation.keep_signals,
        .mask = {},
    };
    // A signal arriving before the child has reset its handlers would run
    // this process's in it: they wait until it has. A crash still ends it.
    sigset_t blocked;
    sigset_t previous;
    sigfillset(&blocked);
    for(int signum: {SIGKILL, SIGSTOP, SIGTRAP, SIGSEGV, SIGBUS, SIGILL, SIGSYS, SIGABRT, SIGFPE}) {
        sigdelset(&blocked, signum);
    }
    ::pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    // The child unblocks them once it has: all of them, or those this
    // thread does not block when it keeps the signals as exec would.
    if(creation.keep_signals) {
        setup.mask = previous;
    } else {
        sigemptyset(&setup.mask);
    }
    // fork rather than vfork: a hook preloaded into this process, as
    // catter's is, runs in the child when it execs, and may allocate.
    pid_t pid = ::fork();
    if(pid == 0) {
        setup.run();
    }
    const int fork_error = errno;
    ::pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    if(pid < 0) {
        return std::unexpected(std::error_code(fork_error, std::generic_category()));
    }
    (*report)[1] = Descriptor();
    // The end of the pipe, at the exec, says it went well; a number, how it
    // failed. Until then this thread waits, so that it is the parent
    // PR_SET_PDEATHSIG watches throughout.
    int code = 0;
    ssize_t got = 0;
    do {
        got = ::read((*report)[0].fd, &code, sizeof(code));
    } while(got < 0 && errno == EINTR);
    if(got > 0) {
        while(::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
        return std::unexpected(std::error_code(code, std::generic_category()));
    }
    child.id = pid;
    child.handle = open_pidfd(pid);
#endif
    return child;
}

}  // namespace kota::sys

#endif
