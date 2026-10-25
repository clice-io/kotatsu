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
//   other <pid>           sets the priority of process <pid> to BelowNormal and
//                         prints what it reads back, as a number, and the
//                         process's resident size
//   title <title> <times> sets the process title to <title> <times> over,
//                         prints the arguments sys::arguments reads then, a
//                         JSON array, and exits once its stdin ends
//   console <text>        Windows: writes <text>, a byte that is no UTF-8
//                         and "!" to a console of its own, and prints what
//                         the console shows
//   job                   Windows: joins a job of at most 1 GiB a process and
//                         one CPU's time, and prints memory().limit and
//                         parallelism()
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
//   orphan                starts `sys_probe hang`, exit_with_parent, its
//                         stdout this one's, prints {"pid": <its pid>} and
//                         exits once its stdin ends, unless killed first
//   orphan-thread         Linux: starts `sys_probe hang` exit_with_parent
//                         from a thread that then ends, and prints
//                         {"signal": <the signal that ended it>}
//   hooked                Linux: starts `sys_probe env KOTA_EXECVE_HOOKED`,
//                         which this one's execve, as a hook preloaded into
//                         it would, sets to 1, and exits as it does
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
//
// Each prints one JSON line to stdout but for hang, and exits with 0; 1 when
// a call fails, saying why on stderr; 2 for a command it does not know.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <format>
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
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "kota/fs/resolve.h"
#include "kota/sys/env.h"
#include "kota/sys/info.h"
#include "kota/sys/process.h"
#include "kota/sys/spawn.h"

#ifdef __linux__
// What a hook preloaded into this process sees, as catter's: every execve it
// makes, which this one marks, setting KOTA_EXECVE_HOOKED=1. The list is a
// static one: in a child between fork and exec nothing may allocate.
extern "C" int execve(const char* path, char* const argv[], char* const envp[]) noexcept {
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

int join_job() {
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit = std::size_t(1) << 30;
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION rate{};
    rate.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
    // In hundredths of a percent of all the CPUs.
    rate.CpuRate = 10000 / ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
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
        print_line(std::format(R"({{"priority":{},"resident":{}}})",
                               static_cast<int>(*priority),
                               stats->resident));
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
    if(command == "hooked") {
        return run(probe({"env", "KOTA_EXECVE_HOOKED"}));
    }
#endif
#ifdef _WIN32
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
    const bool all = arguments.size() == 3 && arguments[2] == "all";
    if(command == "handles" && (arguments.size() == 2 || all)) {
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
    if(command == "job") {
        return join_job();
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
