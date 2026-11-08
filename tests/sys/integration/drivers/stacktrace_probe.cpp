// What sys's stack trace tests start: `stacktrace_probe <command>`. It prints
// what the loader tells of its executable, {"executable": <its path>,
// "base": <its base>, "low": <the lowest offset of its code from the base>,
// "high": <past the highest>}, numbers as hexadecimal strings, and ends in a
// handler that writes the stack with write_stacktrace to stdout and exits
// with 3:
//
//   fault   writes to address 0 in probe_fault, which probe_inlined, forced
//           inline into probe_caller, calls; a SIGSEGV or SIGBUS handler on
//           an alternate stack, on Windows the unhandled exception filter,
//           prints {"fault": <the address the context says it interrupted>}
//           first
//   abort   calls std::abort from probe_abort, which SIGABRT's handler ends
//   late    Windows: loads dbghelp.dll, prints {"library": <its path>}, and
//           writes the stack and exits in probe_callback, which its
//           EnumerateLoadedModulesW64 calls from probe_late; dbghelp would
//           catch a fault there
//
// 2 for a command it does not know.

#include <algorithm>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iterator>
#include <string>
#include <string_view>

#include "kotatsu/support/config.h"
#include "kotatsu/sys/console.h"
#include "kotatsu/sys/stacktrace.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// After windows.h, whose types it uses.
#include <psapi.h>

#include "kotatsu/support/utf16.h"
#define PROBE_ALWAYS_INLINE __forceinline
#else
#include <unistd.h>

#include "kotatsu/sys/process.h"
#define PROBE_ALWAYS_INLINE [[gnu::always_inline]] inline
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#elif defined(__linux__)
#include <link.h>
#include <ucontext.h>
#endif

// Where probe_fault writes, which the compiler cannot tell is null.
volatile int* volatile target = nullptr;

// What the probe functions do after their calls, which keeps those from
// being tail calls that leave no frame.
volatile int calls = 0;

KOTATSU_NOINLINE void probe_fault() {
    *target = 1;
}

PROBE_ALWAYS_INLINE void probe_inlined() {
    probe_fault();
    calls = calls + 1;
}

KOTATSU_NOINLINE void probe_caller() {
    probe_inlined();
    calls = calls + 1;
}

KOTATSU_NOINLINE void probe_abort() {
    std::abort();
}

namespace {

std::string json(std::string_view text) {
    std::string quoted = "\"";
    for(char c: text) {
        if(c == '"' || c == '\\') {
            quoted += '\\';
        }
        quoted += c;
    }
    quoted += '"';
    return quoted;
}

void print_line(std::string_view line) {
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

/// The line {"fault": "0x<address>"}, built without allocating, in a
/// handler.
void print_fault(std::uintptr_t address) {
    char line[48] = R"({"fault":"0x)";
    std::size_t size = std::char_traits<char>::length(line);
    for(int shift = 60; shift >= 0; shift -= 4) {
        line[size++] = "0123456789abcdef"[(address >> shift) & 0xF];
    }
    for(char c: std::string_view("\"}\n")) {
        line[size++] = c;
    }
    kotatsu::sys::write_console(1, std::string_view(line, size));
}

[[noreturn]] void end() {
    kotatsu::sys::write_stacktrace(1);
    std::_Exit(3);
}

/// What the loader tells of the executable.
struct Image {
    std::string path;
    std::uintptr_t base = 0;
    std::uintptr_t low = 0;
    std::uintptr_t high = 0;
};

#ifdef _WIN32

Image executable() {
    HMODULE module = nullptr;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(reinterpret_cast<void*>(&probe_caller)),
                         &module);
    MODULEINFO info{};
    ::K32GetModuleInformation(::GetCurrentProcess(), module, &info, sizeof(info));
    Image image{.base = reinterpret_cast<std::uintptr_t>(module), .high = info.SizeOfImage};
    wchar_t path[MAX_PATH * 4];
    auto size = ::GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    kotatsu::from_utf16(image.path, std::wstring_view(path, size));
    return image;
}

LONG WINAPI filtered(EXCEPTION_POINTERS* exception) {
    print_fault(reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress));
    end();
}

void aborted(int) {
    end();
}

void on_fault() {
    ::SetUnhandledExceptionFilter(filtered);
}

void on_abort() {
    // Without the message box a debug runtime shows first.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::signal(SIGABRT, aborted);
}

using LoadedModules = BOOL(CALLBACK*)(PCWSTR name, DWORD64 base, ULONG size, PVOID context);
using EnumerateLoadedModules = BOOL(WINAPI*)(HANDLE process, LoadedModules callback, PVOID context);

KOTATSU_NOINLINE BOOL CALLBACK probe_callback(PCWSTR, DWORD64, ULONG, PVOID) {
    end();
}

KOTATSU_NOINLINE void probe_late(HMODULE dbghelp) {
    auto enumerate = reinterpret_cast<EnumerateLoadedModules>(
        reinterpret_cast<void*>(::GetProcAddress(dbghelp, "EnumerateLoadedModulesW64")));
    enumerate(::GetCurrentProcess(), probe_callback, nullptr);
    calls = calls + 1;
}

#else

std::uintptr_t interrupted_at(const void* context) {
    const auto* interrupted = static_cast<const ucontext_t*>(context);
#if defined(__APPLE__) && defined(__aarch64__)
    return __darwin_arm_thread_state64_get_pc(interrupted->uc_mcontext->__ss);
#elif defined(__APPLE__)
    return interrupted->uc_mcontext->__ss.__rip;
#elif defined(__aarch64__)
    return interrupted->uc_mcontext.pc;
#else
    return static_cast<std::uintptr_t>(interrupted->uc_mcontext.gregs[REG_RIP]);
#endif
}

Image executable() {
    Image image;
#ifdef __APPLE__
    image.path = ::_dyld_get_image_name(0);
    image.base = static_cast<std::uintptr_t>(::_dyld_get_image_vmaddr_slide(0));
    const auto* header = reinterpret_cast<const mach_header_64*>(::_dyld_get_image_header(0));
    const auto* command = reinterpret_cast<const load_command*>(header + 1);
    for(std::uint32_t index = 0; index < header->ncmds; ++index) {
        if(command->cmd == LC_SEGMENT_64) {
            const auto* segment = reinterpret_cast<const segment_command_64*>(command);
            if(std::string_view(segment->segname) == SEG_TEXT) {
                image.low = segment->vmaddr;
                image.high = segment->vmaddr + segment->vmsize;
            }
        }
        command = reinterpret_cast<const load_command*>(reinterpret_cast<const char*>(command) +
                                                        command->cmdsize);
    }
#else
    kotatsu::sys::executable_path(image.path);
    ::dl_iterate_phdr(
        [](dl_phdr_info* info, std::size_t, void* data) {
            auto& image = *static_cast<Image*>(data);
            image.base = info->dlpi_addr;
            image.low = UINTPTR_MAX;
            for(std::size_t index = 0; index < info->dlpi_phnum; ++index) {
                const auto& segment = info->dlpi_phdr[index];
                if(segment.p_type == PT_LOAD) {
                    image.low = std::min<std::uintptr_t>(image.low, segment.p_vaddr);
                    image.high =
                        std::max<std::uintptr_t>(image.high, segment.p_vaddr + segment.p_memsz);
                }
            }
            return 1;
        },
        &image);
#endif
    return image;
}

void faulted(int, siginfo_t*, void* context) {
    print_fault(interrupted_at(context));
    end();
}

void aborted(int, siginfo_t*, void*) {
    end();
}

void handle(int signal, void (*handler)(int, siginfo_t*, void*)) {
    struct sigaction action = {};
    action.sa_sigaction = handler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    sigaction(signal, &action, nullptr);
}

void on_fault() {
    // An alternate stack, as a crash handler sets one for a stack that
    // overflowed.
    static char room[256 * 1024];
    stack_t alternate = {};
    alternate.ss_sp = room;
    alternate.ss_size = sizeof(room);
    sigaltstack(&alternate, nullptr);
    handle(SIGSEGV, faulted);
    handle(SIGBUS, faulted);
}

void on_abort() {
    handle(SIGABRT, aborted);
}

#endif

void print_image() {
    auto image = executable();
    print_line(std::format(R"({{"executable":{},"base":"{:#x}","low":"{:#x}","high":"{:#x}"}})",
                           json(image.path),
                           image.base,
                           image.low,
                           image.high));
}

}  // namespace

int main(int argc, char** argv) {
    std::string_view command = argc == 2 ? argv[1] : "";
    if(command == "fault") {
        on_fault();
        print_image();
        probe_caller();
        return 1;
    }
    if(command == "abort") {
        on_abort();
        print_image();
        probe_abort();
    }
#ifdef _WIN32
    if(command == "late") {
        print_image();
        HMODULE dbghelp = ::LoadLibraryW(L"dbghelp.dll");
        wchar_t path[MAX_PATH * 4];
        auto size = ::GetModuleFileNameW(dbghelp, path, static_cast<DWORD>(std::size(path)));
        std::string library;
        kotatsu::from_utf16(library, std::wstring_view(path, size));
        print_line(std::format(R"({{"library":{}}})", json(library)));
        probe_late(dbghelp);
        return 1;
    }
#endif
    std::fprintf(stderr, "[error] unknown command\n");
    return 2;
}
