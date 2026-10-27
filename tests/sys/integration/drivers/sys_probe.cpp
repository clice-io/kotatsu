// What sys's integration tests start: `sys_probe <command> [<argument>...]`.
//
//   arguments ...         prints the arguments sys::arguments reads, a JSON
//                         array
//   env <name>            prints the value of variable <name>, a JSON string,
//                         or null when it is unset
//   parent                prints its parent's pid
//   wait                  prints "ready" and exits once its stdin ends
//   moved                 prints "ready", and once its stdin ends, the test
//                         having moved or removed its file meanwhile,
//                         {"path": <what executable_path writes, or null
//                         when it fails>, "code": <the exit code of
//                         `sys_probe parent` started by self_executable, or
//                         null when it cannot be started>}
//   started               prints {"started": <what started_path writes>,
//                         "executable": <what executable_path writes>}
//   other <pid>           sets the priority of process <pid> to BelowNormal and
//                         prints what it reads back, as a number, and the
//                         process's resident size and voluntary switches
//   title <title> <times> sets the process title to <title> <times> over,
//                         prints the arguments sys::arguments reads then, a
//                         JSON array, and exits once its stdin ends
//   console <text>        Windows: writes <text>, a byte that is no UTF-8
//                         and "!" to a console of its own, and prints what
//                         the console shows
//   job [min-max]         Windows: joins a job of at most 1 GiB a process and
//                         one CPU's time, a hard cap or with `min-max` the
//                         maximum of a minimum and maximum rate, and prints
//                         memory().limit and parallelism()
//   hang                  prints nothing and runs until it is killed
//   streams               prints "to-stdout" to stdout and "to-stderr" to
//                         stderr
//   swapped               starts `sys_probe streams`, its stdout this one's
//                         stderr and its stderr this one's stdout, and exits
//                         as it does
//   report <argument>     prints {"arguments": <what sys::arguments reads>,
//                         "cwd": <its working directory>, "value": <the
//                         value of KOTA_PROBE_VALUE>}
//   relay <cwd> <value> <argument> <arg0>
//                         starts `sys_probe report <argument>` in <cwd>, its
//                         argv[0] <arg0>, KOTA_PROBE_VALUE set to <value>,
//                         and exits as it does
//   spawn <program> [<argument>...]
//                         starts <program> with the arguments, its streams
//                         this one's, and exits as it does
//   orphan                starts `sys_probe hang`, exit_with_parent, its
//                         stdout this one's, prints {"pid": <its pid>} and
//                         exits once its stdin ends, unless killed first
//   orphan-thread         Linux: starts `sys_probe hang` exit_with_parent
//                         from a thread that then ends, and prints
//                         {"signal": <the signal that ended it>}
//   switching             Linux: starts a thread that sleeps a thousand
//                         times, prints {"switches": <the times that gave
//                         the CPU up>} and exits once its stdin ends, the
//                         thread there till then
//   hooked                Linux: starts `sys_probe env KOTA_EXECVE_HOOKED`,
//                         which this one's execve, as a hook preloaded into
//                         it would, sets to 1, and exits as it does
//   traced                Linux: starts `sys_probe traceme`, waits for it to
//                         stop, and prints {"running": <whether try_wait
//                         takes it to be running then>, "signal": <the
//                         signal that ended it once killed>}
//   traceme               Linux: asks to be traced by its parent and stops,
//                         as a debugger's child does
//   unblocking            Linux: starts a child on a non-blocking pipe, whose
//                         exec a hook holds and then fails, and meanwhile
//                         another on the same pipe, and prints {"waited":
//                         <whether the second waited for the first to
//                         fail>, "non_blocking": <whether the pipe is left
//                         non-blocking>}
//   reaped                Linux: starts a child that exits instead of its
//                         exec, leaving a process of its own to hold the
//                         spawn back until another thread has reaped it, and
//                         prints {"pid": <the Pid its pidfd's fdinfo shows,
//                         or null when it has none>}; {"pidfds": false} where
//                         this process may open none
//   reaping               Linux: SIGCHLD ignored, starts a child from a thread
//                         a hook holds where spawn sets SIGCHLD's default, and
//                         meanwhile another, sets a handler once that has
//                         started, as kota::process sets one, and prints
//                         {"waited": <whether the second waited for the
//                         first>, "kept": <whether the handler stayed>}
//   nested                Windows: joins a job as a CI runner's, starts
//                         `sys_probe hang` exit_with_parent in a tree of its
//                         own, and prints whether that is in the runner's
//                         job: {"in_job": <bool>}
//   handles [all]         Windows: makes an inheritable event of a name of its
//                         own, starts `sys_probe handle <it> <name>`, with
//                         windows_inherit_handles when `all` is given, and
//                         prints whether that saw it: {"inherited": <bool>}
//   handle <value> <name> Windows: exits with 1 when <value> is a handle of
//                         the event named <name>, 0 otherwise
//   open                  Windows: exits with 8 plus a number whose bit i
//                         says that it has a handle for standard stream i:
//                         its stdout may be the stream it has not
//   closed <i>            starts a child that exits as open does, its
//                         standard stream <i> closed and the others this
//                         one's, and prints {"open": <what that exited with>,
//                         "kept": <whether this one still has stream <i>>};
//                         on Windows `sys_probe open`, in a console this one
//                         shares, elsewhere the shell
//
// Each prints one JSON line to stdout and exits with 0 unless its entry says
// otherwise; 1 when a call fails, saying why on stderr; 2 for a command it
// does not know.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <format>
#include <future>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <io.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "kota/support/utf16.h"
#include "kota/fs/windows.h"
#include "kota/sys/console.h"
#elif defined(__linux__)
#include <cerrno>
#include <csignal>
#include <dlfcn.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef _WIN32
#include <fcntl.h>
#endif

#include "kota/fs/file.h"
#include "kota/fs/resolve.h"
#include "kota/sys/env.h"
#include "kota/sys/info.h"
#include "kota/sys/process.h"
#include "kota/sys/spawn.h"

#ifdef __linux__
// For C libraries whose headers are older than it (Linux 5.3).
#if !defined(SYS_pidfd_open) && !defined(__alpha__) && !defined(__mips__)
#define SYS_pidfd_open 434
#endif

/// What the child of a spawn from a thread that sets it does instead of its
/// exec, as a hook preloaded into this process might; the child has the
/// thread's copy.
thread_local int (*instead_of_exec)() = nullptr;

/// Whether the next sigaction setting SIGCHLD from this thread waits for
/// `gate` to open.
thread_local bool hold_sigchld = false;

/// Two pipes a thread, or a process it forks, waits at: it says it has come
/// through `entered`, and goes on once the gate opens.
struct Gate {
    int entered[2] = {-1, -1};
    int go[2] = {-1, -1};

    bool make() {
        return ::pipe(entered) == 0 && ::pipe(go) == 0;
    }

    /// From the thread or process that waits: async-signal-safe.
    void wait() const {
        char byte = 0;
        while(::write(entered[1], &byte, 1) < 0 && errno == EINTR) {}
        while(::read(go[0], &byte, 1) < 0 && errno == EINTR) {}
    }

    void wait_entered() const {
        char byte = 0;
        while(::read(entered[0], &byte, 1) < 0 && errno == EINTR) {}
    }

    void open() const {
        char byte = 0;
        while(::write(go[1], &byte, 1) < 0 && errno == EINTR) {}
    }
};

constinit Gate gate;

// What a hook preloaded into this process sees, as catter's: every execve it
// makes, which this one marks, setting KOTA_EXECVE_HOOKED=1. The list is a
// static one: in a child between fork and exec nothing may allocate.
extern "C" int execve(const char* path, char* const argv[], char* const envp[]) noexcept {
    if(instead_of_exec != nullptr) {
        return instead_of_exec();
    }
    static char marker[] = "KOTA_EXECVE_HOOKED=1";
    static char* marked[4096];
    std::size_t count = 0;
    marked[count++] = marker;
    for(std::size_t i = 0; envp[i] != nullptr && count < std::size(marked) - 1; ++i) {
        marked[count++] = envp[i];
    }
    marked[count] = nullptr;
    return static_cast<int>(::syscall(SYS_execve, path, argv, marked));
}

// Every sigaction this process makes, libc's own aside, held at `gate` where
// hold_sigchld asks.
extern "C" int sigaction(int signum,
                         const struct sigaction* action,
                         struct sigaction* old) noexcept {
    using sigaction_fn = int (*)(int, const struct sigaction*, struct sigaction*);
    const static auto next = reinterpret_cast<sigaction_fn>(::dlsym(RTLD_NEXT, "sigaction"));
    if(hold_sigchld && signum == SIGCHLD && action != nullptr) {
        hold_sigchld = false;
        gate.wait();
    }
    return next(signum, action, old);
}
#endif

namespace {

std::string json(std::string_view text) {
    std::string quoted = "\"";
    for(char c: text) {
        if(c == '"' || c == '\\') {
            quoted += '\\';
            quoted += c;
        } else if(static_cast<unsigned char>(c) < 0x20) {
            quoted += std::format("\\u{:04x}", static_cast<unsigned char>(c));
        } else {
            quoted += c;
        }
    }
    quoted += '"';
    return quoted;
}

void print_line(std::string_view line) {
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

int fail(std::string_view what, std::error_code error) {
    std::fprintf(stderr,
                 "[error] %.*s: %s\n",
                 static_cast<int>(what.size()),
                 what.data(),
                 error.message().c_str());
    return 1;
}

void wait_for_stdin() {
    while(std::fgetc(stdin) != EOF) {}
}

#ifdef _WIN32

int write_to_console(std::string_view text) {
    // The test's pipes are no console: one of its own, whose screen starts
    // empty. The CRT's stdout stays the test's pipe.
    ::FreeConsole();
    if(!::AllocConsole()) {
        return fail("AllocConsole", kota::fs::windows::last_error());
    }
    HANDLE console = ::CreateFileW(L"CONOUT$",
                                   GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr,
                                   OPEN_EXISTING,
                                   0,
                                   nullptr);
    // Room for the text without scrolling, which a console without a window
    // does past its window's size.
    ::SetConsoleScreenBufferSize(console, COORD{200, 200});
    int fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(console), 0);
    if(auto error = kota::sys::write_console(fd, std::string(text) + "\xFF!")) {
        return fail("write_console", error);
    }
    // Row after row from the top left.
    std::wstring cells(text.size() + 1024, L' ');
    DWORD read = 0;
    ::ReadConsoleOutputCharacterW(console,
                                  cells.data(),
                                  static_cast<DWORD>(cells.size()),
                                  COORD{0, 0},
                                  &read);
    // Blank cells hold a space, or a NUL in a console without a window.
    cells.resize(cells.find_last_not_of(std::wstring_view(L" \0", 2)) + 1);
    std::string shown;
    kota::from_utf16(shown, cells);
    print_line(json(shown));
    return 0;
}

int join_job(bool min_max) {
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit = std::size_t(1) << 30;
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION rate{};
    // In hundredths of a percent of all the CPUs.
    const DWORD one_cpu = 10000 / ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if(min_max) {
        // The maximum in the high half, which MinGW leaves unnamed; a minimum
        // of 0 is refused.
        rate.ControlFlags =
            JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_MIN_MAX_RATE;
        rate.CpuRate = one_cpu << 16 | 1;
    } else {
        rate.ControlFlags =
            JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
        rate.CpuRate = one_cpu;
    }
    if(job == nullptr ||
       !::SetInformationJobObject(job,
                                  JobObjectExtendedLimitInformation,
                                  &limits,
                                  sizeof(limits)) ||
       !::SetInformationJobObject(job, JobObjectCpuRateControlInformation, &rate, sizeof(rate)) ||
       !::AssignProcessToJobObject(job, ::GetCurrentProcess())) {
        return fail("a job", kota::fs::windows::last_error());
    }
    auto memory = kota::sys::memory();
    if(!memory) {
        return fail("memory", memory.error());
    }
    print_line(
        std::format(R"({{"limit":{},"parallelism":{}}})", memory->limit, kota::sys::parallelism()));
    return 0;
}

#endif

/// The arguments `arguments` as a JSON array.
std::string json(const std::vector<std::string>& arguments) {
    std::string line = "[";
    for(std::size_t i = 0; i < arguments.size(); ++i) {
        line += i == 0 ? "" : ",";
        line += json(arguments[i]);
    }
    return line + "]";
}

/// `sys_probe <arguments...>`, its streams this one's: no program, which
/// spawn fails for, when this one's cannot be found.
kota::sys::SpawnOptions probe(std::vector<std::string> arguments) {
    std::string self;
    if(kota::sys::self_executable(self)) {
        self.clear();
    }
    return {.program = std::move(self), .args = std::move(arguments)};
}

/// `sys_probe hang`, its stdout this one's: the test reads it to its end,
/// which comes once the child is gone too.
kota::sys::SpawnOptions hanging(kota::sys::CreationOptions creation) {
    auto options = probe({"hang"});
    options.creation = creation;
    options.streams[0] = kota::sys::Stdio::ignore();
    options.streams[2] = kota::sys::Stdio::ignore();
    return options;
}

/// Whether this process has standard stream `index` (0 to 2): on Windows a
/// handle there.
bool has_stream(int index) {
#ifdef _WIN32
    constexpr DWORD standard[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    HANDLE handle = ::GetStdHandle(standard[index]);
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
#else
    return ::fcntl(index, F_GETFD) != -1;
#endif
}

/// Runs `options`' child to its exit, and exits with its code.
int run(const kota::sys::SpawnOptions& options) {
    auto child = kota::sys::spawn(options);
    if(!child) {
        return fail("spawn", child.error());
    }
    auto status = child->wait();
    if(!status) {
        return fail("wait", status.error());
    }
    // A signal's end, as a shell tells it.
    if(status->term_signal != 0) {
        return 128 + status->term_signal;
    }
    return static_cast<int>(status->code);
}

#ifdef __linux__
/// How long a spawn that should wait for another is given to show that it
/// does not: one that does not wait has its child started well within it.
constexpr std::chrono::seconds waits(1);

/// Instead of the exec: waits at `gate`, then fails as for a missing file.
int held_then_failed() {
    gate.wait();
    errno = ENOENT;
    return -1;
}

int unblocking() {
    int stream[2];
    if(!gate.make() || ::pipe(stream) != 0 ||
       ::fcntl(stream[1], F_SETFL, ::fcntl(stream[1], F_GETFL) | O_NONBLOCK) != 0) {
        return fail("pipe", std::error_code(errno, std::generic_category()));
    }
    auto options = probe({"parent"});
    options.streams[1] = kota::sys::Stdio::from_fd(stream[1]);
    std::thread first([&] {
        instead_of_exec = held_then_failed;
        [[maybe_unused]] auto failed = kota::sys::spawn(options);
    });
    // The first's child has the stream, cleared, and its exec is held.
    gate.wait_entered();
    auto second = std::async(std::launch::async, [&] { return run(options); });
    const bool waited = second.wait_for(waits) == std::future_status::timeout;
    gate.open();
    first.join();
    [[maybe_unused]] auto code = second.get();
    const bool non_blocking = (::fcntl(stream[1], F_GETFL) & O_NONBLOCK) != 0;
    print_line(std::format(R"({{"waited":{},"non_blocking":{}}})", waited, non_blocking));
    return 0;
}

/// Instead of the exec: leaves a process that holds what the child has of
/// its spawn's report, waiting at `gate`, and exits.
int left_a_holder() {
    if(::fork() == 0) {
        gate.wait();
    }
    ::_exit(0);
}

int reaped() {
#ifdef SYS_pidfd_open
    const int own = static_cast<int>(::syscall(SYS_pidfd_open, ::getpid(), 0));
#else
    // The C library names no pidfd_open (Alpha and MIPS with older headers),
    // nor does spawn open one.
    const int own = -1;
#endif
    if(own < 0) {
        print_line(R"({"pidfds":false})");
        return 0;
    }
    ::close(own);
    if(!gate.make()) {
        return fail("pipe", std::error_code(errno, std::generic_category()));
    }
    // Another reaper, as a waitpid(-1) elsewhere in a program is.
    std::thread reaper([] {
        gate.wait_entered();
        while(::waitpid(-1, nullptr, 0) < 0 && errno == EINTR) {}
        gate.open();
    });
    instead_of_exec = left_a_holder;
    auto child = kota::sys::spawn(probe({"parent"}));
    instead_of_exec = nullptr;
    reaper.join();
    if(!child) {
        return fail("spawn", child.error());
    }
    std::string pid = "null";
    if(child->native_handle() >= 0) {
        std::string info;
        if(auto error =
               kota::fs::read_file(info,
                                   std::format("/proc/self/fdinfo/{}", child->native_handle()))) {
            return fail("fdinfo", error);
        }
        constexpr std::string_view field = "Pid:\t";
        auto at = info.find(field);
        if(at != std::string::npos) {
            at += field.size();
            pid = info.substr(at, info.find('\n', at) - at);
        }
    }
    print_line(std::format(R"({{"pid":{}}})", pid));
    return 0;
}

void on_sigchld(int) {}

int reaping() {
    struct sigaction ignored = {};
    ignored.sa_handler = SIG_IGN;
    if(!gate.make() || ::sigaction(SIGCHLD, &ignored, nullptr) != 0) {
        return fail("setup", std::error_code(errno, std::generic_category()));
    }
    auto options = probe({"parent"});
    options.streams[1] = kota::sys::Stdio::ignore();
    std::thread first([&] {
        hold_sigchld = true;
        run(options);
    });
    // The first found SIGCHLD ignored, and is about to set its default.
    gate.wait_entered();
    auto second = std::async(std::launch::async, [&] { return kota::sys::spawn(options); });
    const bool waited = second.wait_for(waits) == std::future_status::timeout;
    // The second's spawn returns once the first has set it.
    if(waited) {
        gate.open();
    }
    auto child = second.get();
    struct sigaction handler = {};
    handler.sa_handler = on_sigchld;
    ::sigaction(SIGCHLD, &handler, nullptr);
    if(!waited) {
        gate.open();
    }
    first.join();
    struct sigaction now = {};
    ::sigaction(SIGCHLD, nullptr, &now);
    if(child) {
        [[maybe_unused]] auto status = child->wait();
    }
    print_line(std::format(R"({{"waited":{},"kept":{}}})", waited, now.sa_handler == on_sigchld));
    return 0;
}
#endif

}  // namespace

int main() {
    namespace sys = kota::sys;
    const auto& arguments = sys::arguments();
    std::string_view command = arguments.size() > 1 ? std::string_view(arguments[1]) : "";

    if(command == "arguments") {
        print_line(json(arguments));
        return 0;
    }
    if(command == "env" && arguments.size() == 3) {
        std::string value;
        print_line(sys::get_env(value, arguments[2]) ? json(value) : "null");
        return 0;
    }
    if(command == "parent") {
        print_line(std::to_string(sys::parent_pid()));
        return 0;
    }
    if(command == "wait") {
        print_line(R"("ready")");
        wait_for_stdin();
        return 0;
    }
    if(command == "moved") {
        print_line(R"("ready")");
        wait_for_stdin();
        std::string path;
        const bool found = !sys::executable_path(path);
        auto options = probe({"parent"});
        options.streams[1] = sys::Stdio::ignore();
        std::string code = "null";
        if(auto child = sys::spawn(options)) {
            if(auto status = child->wait()) {
                code = std::to_string(status->code);
            }
        }
        print_line(std::format(R"({{"path":{},"code":{}}})", found ? json(path) : "null", code));
        return 0;
    }
    if(command == "started") {
        std::string started;
        sys::started_path(started);
        std::string executable;
        if(auto error = sys::executable_path(executable)) {
            return fail("executable_path", error);
        }
        print_line(
            std::format(R"({{"started":{},"executable":{}}})", json(started), json(executable)));
        return 0;
    }
    if(command == "other" && arguments.size() == 3) {
        int pid = std::atoi(arguments[2].c_str());
        if(auto error = sys::set_priority(sys::Priority::BelowNormal, pid)) {
            return fail("set_priority", error);
        }
        auto priority = sys::priority(pid);
        if(!priority) {
            return fail("priority", priority.error());
        }
        auto stats = sys::process_stats(pid);
        if(!stats) {
            return fail("process_stats", stats.error());
        }
        print_line(std::format(R"({{"priority":{},"resident":{},"switches":{}}})",
                               static_cast<int>(*priority),
                               stats->resident,
                               stats->voluntary_switches));
        return 0;
    }
    if(command == "hang") {
        while(true) {
            std::this_thread::sleep_for(std::chrono::hours(1));
        }
    }
    if(command == "streams") {
        print_line(R"("to-stdout")");
        std::fprintf(stderr, "\"to-stderr\"\n");
        return 0;
    }
    if(command == "swapped") {
        auto options = probe({"streams"});
        options.streams = {sys::Stdio::ignore(), sys::Stdio::from_fd(2), sys::Stdio::from_fd(1)};
        return run(options);
    }
    if(command == "report" && arguments.size() == 3) {
        std::string cwd;
        if(auto error = kota::fs::current_dir(cwd)) {
            return fail("current_dir", error);
        }
        std::string value;
        const bool set = sys::get_env(value, "KOTA_PROBE_VALUE");
        print_line(std::format(R"({{"arguments":{},"cwd":{},"value":{}}})",
                               json(arguments),
                               json(cwd),
                               set ? json(value) : "null"));
        return 0;
    }
    if(command == "relay" && arguments.size() == 6) {
        auto options = probe({"report", arguments[4]});
        options.arg0 = arguments[5];
        options.cwd = arguments[2];
        options.env_changes = {
            {.name = "KOTA_PROBE_VALUE", .value = arguments[3]}
        };
        return run(options);
    }
    if(command == "spawn" && arguments.size() >= 3) {
        return run({
            .program = arguments[2],
            .args = std::vector<std::string>(arguments.begin() + 3, arguments.end()),
        });
    }
    if(command == "closed" && arguments.size() == 3 && arguments[2].size() == 1 &&
       arguments[2][0] >= '0' && arguments[2][0] <= '2') {
        const int index = arguments[2][0] - '0';
#ifdef _WIN32
        // One the child shares: a console of its own, which Windows makes
        // for it when this one has none, would fill the null handle.
        ::AllocConsole();
        auto options = probe({"open"});
#else
        // Not this program, which a sanitizer, as it starts, may give a
        // descriptor of its own on a standard stream's number.
        sys::SpawnOptions options{
            .program = "/bin/sh",
            .args = {"-c",
                     "held=8; for i in 0 1 2; do [ -e /dev/fd/$i ] && held=$((held | 1 << i)); "
                     "done; exit $held"},
        };
#endif
        options.streams[index] = sys::Stdio::closed();
        auto child = sys::spawn(options);
        if(!child) {
            return fail("spawn", child.error());
        }
        auto status = child->wait();
        if(!status) {
            return fail("wait", status.error());
        }
        print_line(std::format(R"({{"open":{},"kept":{}}})",
                               status->code,
                               has_stream(index) ? "true" : "false"));
        return 0;
    }
    if(command == "orphan") {
        auto child = sys::spawn(hanging({.exit_with_parent = true}));
        if(!child) {
            return fail("spawn", child.error());
        }
        print_line(std::format(R"({{"pid":{}}})", child->pid()));
        wait_for_stdin();
        return 0;
    }
#ifdef __linux__
    if(command == "orphan-thread") {
        std::expected<sys::Child, std::error_code> child;
        std::thread([&] { child = sys::spawn(hanging({.exit_with_parent = true})); }).join();
        if(!child) {
            return fail("spawn", child.error());
        }
        auto status = child->wait();
        if(!status) {
            return fail("wait", status.error());
        }
        print_line(std::format(R"({{"signal":{}}})", status->term_signal));
        return 0;
    }
    if(command == "switching") {
        std::promise<long> slept;
        std::thread sleeper([&] {
            rusage start{};
            ::getrusage(RUSAGE_THREAD, &start);
            for(int i = 0; i < 1000; ++i) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            rusage end{};
            ::getrusage(RUSAGE_THREAD, &end);
            slept.set_value(end.ru_nvcsw - start.ru_nvcsw);
            wait_for_stdin();
        });
        print_line(std::format(R"({{"switches":{}}})", slept.get_future().get()));
        sleeper.join();
        return 0;
    }
    if(command == "hooked") {
        return run(probe({"env", "KOTA_EXECVE_HOOKED"}));
    }
    if(command == "unblocking") {
        return unblocking();
    }
    if(command == "reaped") {
        return reaped();
    }
    if(command == "reaping") {
        return reaping();
    }
    if(command == "traced") {
        auto child = sys::spawn(probe({"traceme"}));
        if(!child) {
            return fail("spawn", child.error());
        }
        // Its stop, which this process, its tracer, is told of, left there.
        siginfo_t info{};
        if(::waitid(P_PID, static_cast<id_t>(child->pid()), &info, WSTOPPED | WEXITED | WNOWAIT) <
           0) {
            return fail("waitid", std::error_code(errno, std::generic_category()));
        }
        if(info.si_code != CLD_TRAPPED) {
            return fail("waitid", std::make_error_code(std::errc::protocol_error));
        }
        auto running = child->try_wait();
        if(!running) {
            return fail("try_wait", running.error());
        }
        if(auto error = child->kill()) {
            return fail("kill", error);
        }
        auto status = child->wait();
        if(!status) {
            return fail("wait", status.error());
        }
        print_line(std::format(R"({{"running":{},"signal":{}}})",
                               running->has_value() ? "false" : "true",
                               status->term_signal));
        return 0;
    }
    if(command == "traceme") {
        if(::ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0) {
            return fail("ptrace", std::error_code(errno, std::generic_category()));
        }
        ::raise(SIGSTOP);
        return 0;
    }
#endif
#ifdef _WIN32
    if(command == "open") {
        int held = 8;
        for(int index = 0; index < 3; ++index) {
            held |= has_stream(index) ? 1 << index : 0;
        }
        return held;
    }
    if(command == "nested") {
        // As a CI runner's: a job of its own, which its processes' children
        // may leave.
        HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
        if(job == nullptr ||
           !::SetInformationJobObject(job,
                                      JobObjectExtendedLimitInformation,
                                      &limits,
                                      sizeof(limits)) ||
           !::AssignProcessToJobObject(job, ::GetCurrentProcess())) {
            return fail("a job", kota::fs::windows::last_error());
        }
        auto child = sys::spawn(hanging({.process_group = true, .exit_with_parent = true}));
        if(!child) {
            return fail("spawn", child.error());
        }
        BOOL in_job = FALSE;
        ::IsProcessInJob(child->native_handle(), job, &in_job);
        print_line(std::format(R"({{"in_job":{}}})", in_job ? "true" : "false"));
        return 0;
    }
    if(const bool all = arguments.size() == 3 && arguments[2] == "all";
       command == "handles" && (arguments.size() == 2 || all)) {
        SECURITY_ATTRIBUTES inherit{.nLength = sizeof(inherit),
                                    .lpSecurityDescriptor = nullptr,
                                    .bInheritHandle = TRUE};
        auto name = std::format("Local\\kota-probe-{}", ::GetCurrentProcessId());
        std::wstring wide_name;
        kota::to_utf16(wide_name, name);
        HANDLE event = ::CreateEventW(&inherit, TRUE, TRUE, wide_name.c_str());
        if(event == nullptr) {
            return fail("CreateEventW", kota::fs::windows::last_error());
        }
        auto options =
            probe({"handle", std::to_string(reinterpret_cast<std::uintptr_t>(event)), name});
        options.creation.windows_inherit_handles = all;
        auto child = sys::spawn(options);
        if(!child) {
            return fail("spawn", child.error());
        }
        auto status = child->wait();
        if(!status) {
            return fail("wait", status.error());
        }
        print_line(std::format(R"({{"inherited":{}}})", status->code == 1 ? "true" : "false"));
        return 0;
    }
    if(command == "handle" && arguments.size() == 4) {
        // Whether the number is a handle of that very event: another handle
        // of this process's may have the number, a signaled one too, or the
        // one opened here, where nothing held it.
        using Compare = BOOL(WINAPI*)(HANDLE, HANDLE);
        auto compare = reinterpret_cast<Compare>(reinterpret_cast<void*>(
            ::GetProcAddress(::GetModuleHandleW(L"kernelbase.dll"), "CompareObjectHandles")));
        std::wstring wide_name;
        kota::to_utf16(wide_name, arguments[3]);
        HANDLE named = ::OpenEventW(SYNCHRONIZE, FALSE, wide_name.c_str());
        if(compare == nullptr || named == nullptr) {
            return 0;
        }
        auto* value = reinterpret_cast<HANDLE>(std::stoull(arguments[2]));
        return value != named && compare(value, named) ? 1 : 0;
    }
    if(command == "console" && arguments.size() == 3) {
        return write_to_console(arguments[2]);
    }
    if(command == "job" &&
       (arguments.size() == 2 || (arguments.size() == 3 && arguments[2] == "min-max"))) {
        return join_job(arguments.size() == 3);
    }
#else
    if(command == "title" && arguments.size() == 4) {
        std::string title;
        for(int i = std::atoi(arguments[3].c_str()); i > 0; --i) {
            title += arguments[2];
        }
        if(auto error = sys::set_process_title(title)) {
            return fail("set_process_title", error);
        }
        print_line(json(sys::arguments()));
        wait_for_stdin();
        return 0;
    }
#endif
    std::fprintf(stderr, "[error] unknown command\n");
    return 2;
}
