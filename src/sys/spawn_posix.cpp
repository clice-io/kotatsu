#ifndef _WIN32

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <spawn.h>
#include <string>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "child.h"
#include "os.h"
#include "kota/sys/spawn.h"

#ifdef __APPLE__
#include <crt_externs.h>
#else
#include <sys/prctl.h>
#include <sys/syscall.h>

// pidfd_open's number on every architecture but Alpha's and MIPS's, for C
// libraries whose headers are older than it (Linux 5.3).
#if !defined(SYS_pidfd_open) && !defined(__alpha__) && !defined(__mips__)
#define SYS_pidfd_open 434
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
std::error_code connected_pair(std::array<Descriptor, 2>& pair, bool socket) {
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
        return errno_code();
    }
    pair[0] = Descriptor(fds[0]);
    pair[1] = Descriptor(fds[1]);
    if(socket) {
        // What libuv gives a child's socket pair, macOS's own being 8 KiB.
        int size = 64 * 1024;
        for(int fd: fds) {
            ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
            ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
        }
    }
    return {};
}

/// How the child ended, from wait4.
ExitStatus exit_status_of(int status, const rusage& usage) {
    auto microseconds = [](const timeval& time) {
        return std::chrono::seconds(time.tv_sec) + std::chrono::microseconds(time.tv_usec);
    };
    return ExitStatus{
        .status = WIFEXITED(status) ? WEXITSTATUS(status) : 0,
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
    char* const* envp;
    /// The descriptor each stream takes; -1 for /dev/null.
    std::array<int, 3> fds;
    const char* cwd;
    bool detached;
    bool process_group;
    bool exit_with_parent;
    pid_t parent;
    int error_fd;

    [[noreturn]] void run() {
        // Handlers mean nothing to another program, and an ignored signal
        // stays ignored across exec. Past 31 are glibc's own.
        for(int signum = 1; signum < 32; ++signum) {
            if(signum != SIGKILL && signum != SIGSTOP && ::signal(signum, SIG_DFL) == SIG_ERR) {
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
            if(fd < 0) {
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
            // Shared with whoever else has it open, as libuv does: a child
            // given a non-blocking stream would see EAGAIN.
            int flags = ::fcntl(target, F_GETFL);
            if(flags >= 0 && (flags & O_NONBLOCK)) {
                ::fcntl(target, F_SETFL, flags & ~O_NONBLOCK);
            }
        }
        if(cwd != nullptr && ::chdir(cwd) < 0) {
            fail_in_child(error_fd);
        }
        sigset_t none;
        sigemptyset(&none);
        ::sigprocmask(SIG_SETMASK, &none, nullptr);
        // libc's execve, which a hook preloaded into this process sees.
        ::execve(path, argv, envp);
        fail_in_child(error_fd);
    }
};

/// A pidfd of child `pid`, or -1 where the kernel has none or a sandbox
/// forbids it; it stays valid until the child is reaped, by us alone.
int open_pidfd([[maybe_unused]] pid_t pid) {
#ifdef SYS_pidfd_open
    return static_cast<int>(::syscall(SYS_pidfd_open, pid, 0));
#else
    return -1;
#endif
}

#endif

}  // namespace

Child::Child(Child&& other) noexcept :
    id(std::exchange(other.id, -1)), handle(std::exchange(other.handle, invalid_handle)),
    pipes(std::exchange(other.pipes, {fs::File::closed, fs::File::closed, fs::File::closed})),
    exited(std::exchange(other.exited, std::nullopt)),
    leads_group(std::exchange(other.leads_group, false)) {}

Child& Child::operator=(Child&& other) noexcept {
    Child gone(std::move(*this));
    id = std::exchange(other.id, -1);
    handle = std::exchange(other.handle, invalid_handle);
    pipes = std::exchange(other.pipes, {fs::File::closed, fs::File::closed, fs::File::closed});
    exited = std::exchange(other.exited, std::nullopt);
    leads_group = std::exchange(other.leads_group, false);
    return *this;
}

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

fs::File::native_handle_type Child::take_pipe(int index) noexcept {
    return std::exchange(pipes[static_cast<std::size_t>(index)], fs::File::closed);
}

std::expected<std::optional<ExitStatus>, std::error_code> Child::try_wait() {
    if(id < 0) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    if(exited) {
        return exited;
    }
    int status = 0;
    rusage usage{};
    pid_t reaped = 0;
    do {
        reaped = ::wait4(id, &status, WNOHANG, &usage);
    } while(reaped < 0 && errno == EINTR);
    if(reaped < 0) {
        return std::unexpected(errno_code());
    }
    if(reaped == 0) {
        return std::nullopt;
    }
    exited = exit_status_of(status, usage);
    if(handle >= 0) {
        ::close(std::exchange(handle, invalid_handle));
    }
    return exited;
}

std::expected<ExitStatus, std::error_code> Child::wait() {
    if(id < 0) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    if(exited) {
        return *exited;
    }
    int status = 0;
    rusage usage{};
    pid_t reaped = 0;
    do {
        reaped = ::wait4(id, &status, 0, &usage);
    } while(reaped < 0 && errno == EINTR);
    if(reaped < 0) {
        return std::unexpected(errno_code());
    }
    exited = exit_status_of(status, usage);
    if(handle >= 0) {
        ::close(std::exchange(handle, invalid_handle));
    }
    return *exited;
}

std::error_code Child::kill(int signum) {
    if(id < 0) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    // Once it is reaped its pid may be another process's.
    if(exited) {
        return std::make_error_code(std::errc::no_such_process);
    }
    if(::kill(id, signum) != 0) {
        return errno_code();
    }
    return {};
}

std::error_code Child::kill() {
    return kill(SIGKILL);
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

std::error_code Child::kill_tree() {
    return kill_tree(SIGKILL);
}

std::expected<Child, std::error_code> spawn(const SpawnOptions& options) {
    const auto& creation = options.creation;
#ifdef __APPLE__
    if(creation.exit_with_parent) {
        return std::unexpected(std::make_error_code(std::errc::not_supported));
    }
#endif
    auto environment = child_environment(options);
    auto program = child_program(options, environment);
    if(!program) {
        return std::unexpected(program.error());
    }

    // What the child gets, laid out before it starts.
    std::vector<std::string> args = options.args;
    if(args.empty()) {
        args.push_back(options.file);
    }
    std::vector<char*> argv;
    for(auto& arg: args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    std::vector<std::string> variables;
    std::vector<char*> envp;
    if(environment) {
        for(const auto& [name, value]: *environment) {
            variables.push_back(std::format("{}={}", name, value));
        }
        for(auto& variable: variables) {
            envp.push_back(variable.data());
        }
        envp.push_back(nullptr);
    }
#ifdef __APPLE__
    char* const* child_envp = environment ? envp.data() : *::_NSGetEnviron();
#else
    char* const* child_envp = environment ? envp.data() : environ;
#endif

    Child child;
    // The child's end of each pipe, closed here once it has started.
    std::array<Descriptor, 3> child_ends;
    std::array<int, 3> fds = {-1, -1, -1};
    for(std::size_t i = 0; i < 3; ++i) {
        const auto& stream = options.streams[i];
        switch(stream.kind) {
            case Stdio::Kind::Inherit: fds[i] = static_cast<int>(i); break;
            case Stdio::Kind::Ignore: break;
            case Stdio::Kind::FD:
            case Stdio::Kind::File: {
                int fd = stream.kind == Stdio::Kind::FD ? stream.fd : stream.file;
                if(fd < 0) {
                    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
                }
                fds[i] = fd;
                break;
            }
            case Stdio::Kind::Pipe: {
                const bool both = stream.readable && stream.writable;
                std::array<Descriptor, 2> pair;
                if(auto error = connected_pair(pair, both)) {
                    return std::unexpected(error);
                }
                // A pipe the child reads it reads from [0].
                const std::size_t ours = both || stream.readable ? 1 : 0;
                child.pipes[i] = pair[ours].release();
                child_ends[i] = std::move(pair[1 - ours]);
                fds[i] = child_ends[i].fd;
                break;
            }
        }
    }
    child.leads_group = creation.detached || creation.process_group;

#ifdef __APPLE__
    // posix_spawn, a system call of its own on macOS: no fork of this
    // process, however large, and nothing of it runs in the child.
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    // A session's leader leads its group too; macOS runs SETPGROUP before
    // SETSID, so never both.
    short flags = POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK;
    if(creation.detached) {
        flags |= POSIX_SPAWN_SETSID;
    } else if(creation.process_group) {
        flags |= POSIX_SPAWN_SETPGROUP;
    }
    posix_spawnattr_setflags(&attributes, flags);
    posix_spawnattr_setpgroup(&attributes, 0);
    sigset_t signals;
    sigfillset(&signals);
    posix_spawnattr_setsigdefault(&attributes, &signals);
    sigemptyset(&signals);
    posix_spawnattr_setsigmask(&attributes, &signals);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if(!options.cwd.empty()) {
        posix_spawn_file_actions_addchdir_np(&actions, options.cwd.c_str());
    }
    // A descriptor below the stream it goes to moves above them first, to a
    // number no stream's descriptor has.
    std::array<int, 3> sources = fds;
    for(int target = 0; target < 3; ++target) {
        if(sources[target] >= 0 && sources[target] < target) {
            int free = 3;
            while(std::ranges::find(fds, free) != fds.end()) {
                ++free;
            }
            posix_spawn_file_actions_adddup2(&actions, sources[target], free);
            sources[target] = free;
        }
    }
    for(int target = 0; target < 3; ++target) {
        int fd = sources[target];
        if(fd < 0) {
            posix_spawn_file_actions_addopen(&actions,
                                             target,
                                             "/dev/null",
                                             target == 0 ? O_RDONLY : O_RDWR,
                                             0);
        } else if(fd == target) {
            posix_spawn_file_actions_addinherit_np(&actions, fd);
        } else {
            posix_spawn_file_actions_adddup2(&actions, fd, target);
        }
        if(fd >= 0) {
            // Shared with whoever else has it open, as libuv does.
            int status = ::fcntl(fd, F_GETFL);
            if(status >= 0 && (status & O_NONBLOCK)) {
                ::fcntl(fd, F_SETFL, status & ~O_NONBLOCK);
            }
        }
    }
    // The descriptors moved into place need not stay twice.
    for(int target = 0; target < 3; ++target) {
        int fd = sources[target];
        if(fd >= 3 && std::ranges::find(sources.begin(), sources.begin() + target, fd) ==
                          sources.begin() + target) {
            posix_spawn_file_actions_addclose(&actions, fd);
        }
    }
    pid_t pid = -1;
    int failed = 0;
    do {
        failed =
            ::posix_spawn(&pid, program->c_str(), &actions, &attributes, argv.data(), child_envp);
    } while(failed == EINTR);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if(failed != 0) {
        return std::unexpected(std::error_code(failed, std::generic_category()));
    }
    child.id = pid;
#else
    std::array<Descriptor, 2> report;
    if(auto error = connected_pair(report, false)) {
        return std::unexpected(error);
    }
    ChildSetup setup{
        .path = program->c_str(),
        .argv = argv.data(),
        .envp = child_envp,
        .fds = fds,
        .cwd = options.cwd.empty() ? nullptr : options.cwd.c_str(),
        .detached = creation.detached,
        .process_group = creation.process_group,
        .exit_with_parent = creation.exit_with_parent,
        .parent = ::getpid(),
        .error_fd = report[1].fd,
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
    report[1] = Descriptor();
    // The end of the pipe, at the exec, says it went well; a number, how it
    // failed. Until then this thread waits, so that it is the parent
    // PR_SET_PDEATHSIG watches throughout.
    int code = 0;
    ssize_t got = 0;
    do {
        got = ::read(report[0].fd, &code, sizeof(code));
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
