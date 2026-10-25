// What sys's integration tests start: `sys_probe <command> [<argument>...]`.
//
//   arguments ...         prints the arguments sys::arguments reads, a JSON
//                         array
//   env <name>            prints the value of variable <name>, a JSON string,
//                         or null when it is unset
//   parent                prints its parent's pid
//   wait                  prints "ready" and exits once its stdin ends
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
//   orphan                starts `sys_probe wait`, exit_with_parent, its stdin
//                         its own, prints {"pid": <its pid>} and exits once
//                         its stdin ends, unless killed first
//   orphan-thread         the same, started from a thread that then ends
//   nested                Windows: joins a job as a CI runner's, starts
//                         `sys_probe wait` exit_with_parent in a tree of its
//                         own, and prints whether it is in a job
//   handles               Windows: makes an inheritable event, starts
//                         `sys_probe handle <it>`, and prints whether that saw
//                         it: {"inherited": <bool>}
//   handle <value>        Windows: exits with 1 when <value> is a signaled
//                         event's handle, 0 otherwise
//
// Each prints one JSON line, and exits with 0; 1 when a call fails, saying
// why on stderr; 2 for a command it does not know.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <format>
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
#endif

#include "kota/sys/env.h"
#include "kota/sys/info.h"
#include "kota/sys/process.h"
#include "kota/sys/spawn.h"

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
    print_line(json(kota::from_utf16(std::wstring_view(cells))));
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

}  // namespace

/// `sys_probe wait`, which runs until its stdin ends: this one's, which the
/// test holds.
kota::sys::SpawnOptions waiter(kota::sys::CreationOptions creation) {
    auto self = kota::sys::executable_path();
    kota::sys::SpawnOptions options{
        .file = self ? *self : std::string(),
        .args = {self ? *self : std::string(), "wait"},
        .creation = creation,
    };
    options.streams[1] = kota::sys::Stdio::ignore();
    return options;
}

/// Prints the pid of `child`, started, and exits once its stdin ends.
int report_and_wait(std::expected<kota::sys::Child, std::error_code>& child) {
    if(!child) {
        return fail("spawn", child.error());
    }
    print_line(std::format(R"({{"pid":{}}})", child->pid()));
    wait_for_stdin();
    return 0;
}

/// The arguments `arguments` as a JSON array.
std::string json(const std::vector<std::string>& arguments) {
    std::string line = "[";
    for(std::size_t i = 0; i < arguments.size(); ++i) {
        line += i == 0 ? "" : ",";
        line += json(arguments[i]);
    }
    return line + "]";
}

int main() {
    namespace sys = kota::sys;
    const auto& arguments = sys::arguments();
    std::string_view command = arguments.size() > 1 ? std::string_view(arguments[1]) : "";

    if(command == "arguments") {
        print_line(json(arguments));
        return 0;
    }
    if(command == "env" && arguments.size() == 3) {
        auto value = sys::get_env(arguments[2]);
        print_line(value ? json(*value) : "null");
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
    if(command == "orphan") {
        auto child = sys::spawn(waiter({.exit_with_parent = true}));
        return report_and_wait(child);
    }
    if(command == "orphan-thread") {
        std::expected<sys::Child, std::error_code> child;
        std::thread([&] { child = sys::spawn(waiter({.exit_with_parent = true})); }).join();
        return report_and_wait(child);
    }
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
        auto child = sys::spawn(waiter({.process_group = true, .exit_with_parent = true}));
        if(!child) {
            return fail("spawn", child.error());
        }
        BOOL in_job = FALSE;
        ::IsProcessInJob(child->native_handle(), nullptr, &in_job);
        print_line(std::format(R"({{"in_job":{}}})", in_job ? "true" : "false"));
        return 0;
    }
    if(command == "handles") {
        SECURITY_ATTRIBUTES inherit{.nLength = sizeof(inherit),
                                    .lpSecurityDescriptor = nullptr,
                                    .bInheritHandle = TRUE};
        HANDLE event = ::CreateEventW(&inherit, TRUE, TRUE, nullptr);
        auto self = sys::executable_path();
        auto child = sys::spawn({
            .file = self ? *self : std::string(),
            .args = {self ? *self : std::string(),
                     "handle", std::to_string(reinterpret_cast<std::uintptr_t>(event))},
        });
        if(!child) {
            return fail("spawn", child.error());
        }
        auto status = child->wait();
        if(!status) {
            return fail("wait", status.error());
        }
        print_line(std::format(R"({{"inherited":{}}})", status->status == 1 ? "true" : "false"));
        return 0;
    }
    if(command == "handle" && arguments.size() == 3) {
        auto* event = reinterpret_cast<HANDLE>(std::stoull(arguments[2]));
        return ::WaitForSingleObject(event, 0) == WAIT_OBJECT_0 ? 1 : 0;
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
