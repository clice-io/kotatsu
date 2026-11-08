// What sys's stack trace tests start: `stacktrace_probe <command>`. It prints
// what the loader tells of its executable, {"executable": <its path>,
// "base": <its base>, "low": <the lowest offset of its code from the base>,
// "high": <past the highest>, "debug": "pdb" or "dwarf"}, numbers as
// hexadecimal strings, and ends in a handler that prints the probes' return
// addresses as {"returns": [<innermost first>, ...]}, writes the stack with
// write_stacktrace to stdout and exits with 3:
//
//   fault   writes to address 16 in probe_fault, which probe_inlined, forced
//           inline into probe_caller, calls; a SIGSEGV or SIGBUS handler on
//           an alternate stack, on Windows the unhandled exception filter,
//           prints {"fault": <the address the context says it interrupted>}
//           first
//   call    calls a null function pointer from probe_call, handled as fault
//   abort   calls std::abort from probe_abort, which SIGABRT's handler ends
//   early   Linux: with KOTATSU_PROBE_EARLY=1, faults before the library's
//           static initializers, through probe_caller, whatever the command
//   late    Windows: loads dbghelp.dll, prints {"library": <its path>,
//           "loaded_before": <whether it was loaded>, "library_base":
//           <its module handle>}, and
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

#include "sys/harness/json.h"
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

#include "kotatsu/support/utf16.h"
#else
#include <unistd.h>

#include "kotatsu/sys/process.h"
#endif

#if KOTATSU_COMPILER_MSVC
#include <intrin.h>
#define PROBE_ALWAYS_INLINE __forceinline
#define PROBE_RETURN_ADDRESS() _ReturnAddress()
#else
#define PROBE_ALWAYS_INLINE [[gnu::always_inline]] inline
#define PROBE_RETURN_ADDRESS() __builtin_return_address(0)
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#elif defined(__linux__)
#include <link.h>
#include <ucontext.h>
#endif

#if defined(__SANITIZE_ADDRESS__)
#define PROBE_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PROBE_ASAN 1
#endif
#endif

#ifdef PROBE_ASAN
// On Windows ASan would otherwise take the access violation before our filter.
extern "C" const char* __asan_default_options() {
    return "handle_segv=0";
}
#endif

// Where probe_fault writes: an address in the first page, which no host maps,
// and not null, which UBSan would report first.
volatile int* volatile target = reinterpret_cast<volatile int*>(std::uintptr_t(16));

void (*volatile call_target)() = nullptr;

// The callee's return address first, its caller's second. Single-probe
// modes leave the second slot empty; these are ready before any constructor.
volatile std::uintptr_t return_addresses[2] = {};

// What the probe functions do after their calls, which keeps those from
// being tail calls that leave no frame.
volatile int calls = 0;

KOTATSU_NOINLINE void probe_fault() {
    return_addresses[0] = reinterpret_cast<std::uintptr_t>(PROBE_RETURN_ADDRESS());
    *target = 1;
}

PROBE_ALWAYS_INLINE void probe_inlined() {
    probe_fault();
    calls = calls + 1;
}

KOTATSU_NOINLINE void probe_caller() {
    return_addresses[1] = reinterpret_cast<std::uintptr_t>(PROBE_RETURN_ADDRESS());
    probe_inlined();
    calls = calls + 1;
}

KOTATSU_NOINLINE void probe_abort() {
    return_addresses[0] = reinterpret_cast<std::uintptr_t>(PROBE_RETURN_ADDRESS());
    std::abort();
}

#ifdef __clang__
// UBSan's function-type check reads before the target; let the call reach zero.
[[clang::no_sanitize("function")]]
#endif
KOTATSU_NOINLINE void probe_call() {
    return_addresses[0] = reinterpret_cast<std::uintptr_t>(PROBE_RETURN_ADDRESS());
    call_target();
    calls = calls + 1;
}

namespace {

using kotatsu::test::json;

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

/// The recorded return addresses, built without allocating in a handler.
void print_returns() {
    char line[64] = R"({"returns":[)";
    std::size_t size = std::char_traits<char>::length(line);
    bool first = true;
    for(auto address: return_addresses) {
        if(address == 0) {
            continue;
        }
        if(!first) {
            line[size++] = ',';
        }
        first = false;
        for(char c: std::string_view(R"("0x)")) {
            line[size++] = c;
        }
        for(int shift = 60; shift >= 0; shift -= 4) {
            line[size++] = "0123456789abcdef"[(address >> shift) & 0xF];
        }
        line[size++] = '"';
    }
    for(char c: std::string_view("]}\n")) {
        line[size++] = c;
    }
    kotatsu::sys::write_console(1, std::string_view(line, size));
}

}  // namespace

// Outside the anonymous namespace, as the probe functions are: a MinGW
// executable's COFF symbols, which llvm-symbolizer names its functions by,
// leave out functions of internal linkage.
[[noreturn]] KOTATSU_NOINLINE void end() {
    print_returns();
    kotatsu::sys::write_stacktrace(1);
    std::_Exit(3);
}

namespace {

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
    Image image;
    image.base = reinterpret_cast<std::uintptr_t>(module);
    image.low = UINTPTR_MAX;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image.base + dos->e_lfanew);
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    for(std::size_t index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        const auto& section = sections[index];
        if(section.Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            image.low = std::min<std::uintptr_t>(image.low, section.VirtualAddress);
            image.high = std::max<std::uintptr_t>(image.high,
                                                  std::uintptr_t(section.VirtualAddress) +
                                                      section.Misc.VirtualSize);
        }
    }
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

}  // namespace

// Of external linkage, as end() is.
KOTATSU_NOINLINE BOOL CALLBACK probe_callback(PCWSTR, DWORD64, ULONG, PVOID) {
    return_addresses[0] = reinterpret_cast<std::uintptr_t>(PROBE_RETURN_ADDRESS());
    end();
}

KOTATSU_NOINLINE void probe_late(HMODULE dbghelp) {
    return_addresses[1] = reinterpret_cast<std::uintptr_t>(PROBE_RETURN_ADDRESS());
    auto enumerate = reinterpret_cast<EnumerateLoadedModules>(
        reinterpret_cast<void*>(::GetProcAddress(dbghelp, "EnumerateLoadedModulesW64")));
    enumerate(::GetCurrentProcess(), probe_callback, nullptr);
    calls = calls + 1;
}

namespace {

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
                if(segment.p_type == PT_LOAD && (segment.p_flags & PF_X)) {
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
#ifdef _MSC_VER
    constexpr std::string_view debug = "pdb";
#else
    constexpr std::string_view debug = "dwarf";
#endif
    print_line(
        std::format(R"({{"executable":{},"base":"{:#x}","low":"{:#x}","high":"{:#x}","debug":{}}})",
                    json(image.path),
                    image.base,
                    image.low,
                    image.high,
                    json(debug)));
}

#ifdef __linux__
struct Early {
    Early() {
        if(const char* value = std::getenv("KOTATSU_PROBE_EARLY");
           value != nullptr && std::string_view(value) == "1") {
            on_fault();
            print_image();
            probe_caller();
            calls = calls + 1;
        }
    }
};

// Before the library caches module information, exercising /proc/self/maps.
[[gnu::init_priority(101)]] Early early;
#endif

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
    if(command == "call") {
        on_fault();
        print_image();
        probe_call();
        return 1;
    }
#ifdef _WIN32
    if(command == "late") {
        print_image();
        const bool loaded_before = ::GetModuleHandleW(L"dbghelp.dll") != nullptr;
        HMODULE dbghelp = ::LoadLibraryW(L"dbghelp.dll");
        wchar_t path[MAX_PATH * 4];
        auto size = ::GetModuleFileNameW(dbghelp, path, static_cast<DWORD>(std::size(path)));
        std::string library;
        kotatsu::from_utf16(library, std::wstring_view(path, size));
        print_line(std::format(R"({{"library":{},"loaded_before":{},"library_base":"{:#x}"}})",
                               json(library),
                               loaded_before,
                               reinterpret_cast<std::uintptr_t>(dbghelp)));
        probe_late(dbghelp);
        return 1;
    }
#endif
    std::fprintf(stderr, "[error] unknown command\n");
    return 2;
}
