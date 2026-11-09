// What sys's stack trace tests start: `stacktrace_probe <command>`. It prints
// what the loader tells of its executable, {"executable": <its path>,
// "base": <its base>, "low": <the lowest offset of its code from the base>,
// "high": <past the highest>, "debug": "pdb" or "dwarf", "tsan": <whether
// it runs under TSan>, "exceptions": <whether it was built with C++
// exceptions>}, numbers as hexadecimal strings, and ends in a
// handler that prints the probes' return addresses as {"returns":
// [<innermost first>, ...]}, writes the stack with write_stacktrace to
// stdout and exits with 3:
//
//   fault   writes to address 16 in probe_fault, a leaf, which probe_inlined,
//           forced inline into probe_caller, calls; a SIGSEGV or SIGBUS
//           handler on an alternate stack, on Windows the unhandled
//           exception filter, prints {"fault": <the address the context
//           says it interrupted>} first
//   fault_on_stack
//           as fault, the handler on the thread's own stack; on Windows,
//           whose filter runs there, as fault
//   fault_after_call
//           as fault, from probe_fault_after_call, which calls probe_callee
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
// The commands from crash_ on crash through on_crash instead, whose handler
// prints {"signal": <the signal>} and on Windows for an exception
// {"signal": ..., "code": "<its code>"}, then {"fault": ...} for a crash with
// a context, {"returns": ...}, and writes the stack with
// write_stacktrace(1, crash); the process then ends as the crash ends it:
//
//   crash_fault     as fault
//   crash_trap      traps in probe_trap
//   crash_breakpoint
//                   breaks in probe_breakpoint; POSIX, but under TSan:
//                   SIGTRAP's default action set with SA_SIGINFO before
//                   on_crash
//   crash_abort     calls std::abort from probe_abort
//   crash_throw     throws a C++ exception from probe_throw that none
//                   catches; without exceptions exits with 0
//   crash_overflow  overflows the stack in probe_recurse_a and _b, which call
//                   each other
//   crash_overflow_on_thread
//                   as crash_overflow, on a Thread of 1 MiB
//   crash_two       faults in probe_fault_together on two Threads at once,
//                   the handler waiting for both to be about to
//   crash_after_handler
//                   as crash_fault, with a handler of SIGSEGV set before, on
//                   Windows an unhandled exception filter, which prints
//                   {"previous": <the address its context says it
//                   interrupted>} and exits with 4
//   crash_in_handler
//                   POSIX: as crash_fault, the handler faulting again after
//                   its first line
//   crash_raise     POSIX: raises SIGSEGV
//   crash_terminate POSIX: raises SIGTERM, which on_crash leaves alone
//   crash_ignored   POSIX: raises SIGTRAP, ignored before on_crash, then
//                   exits with 5
//
// 2 for a command it does not know.

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include "sys/harness/context.h"
#include "sys/harness/json.h"
#include "kotatsu/support/config.h"
#include "kotatsu/sys/console.h"
#include "kotatsu/sys/crash.h"
#include "kotatsu/sys/stacktrace.h"
#include "kotatsu/sys/thread.h"

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
#endif

#if defined(__SANITIZE_ADDRESS__)
#define PROBE_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PROBE_ASAN 1
#endif
#endif

#if defined(__SANITIZE_THREAD__)
#define PROBE_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define PROBE_TSAN 1
#endif
#endif

#if KOTATSU_COMPILER_MSVC
#define PROBE_NO_ASAN __declspec(no_sanitize_address)
#else
#define PROBE_NO_ASAN [[gnu::no_sanitize_address]]
#endif

// A sanitizer would otherwise take the faults before the probe's handlers,
// or after on_crash's as the action it puts back, on Windows ASan the access
// violation before the filter.
#define PROBE_SANITIZER_OPTIONS "handle_segv=0:handle_sigbus=0:handle_sigfpe=0:handle_sigill=0"

#ifdef PROBE_ASAN
extern "C" const char* __asan_default_options() {
    return PROBE_SANITIZER_OPTIONS;
}
#endif

#ifdef PROBE_TSAN
extern "C" const char* __tsan_default_options() {
    return PROBE_SANITIZER_OPTIONS;
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

// A leaf: it calls nothing, and on arm64 makes no frame record.
KOTATSU_NOINLINE void probe_fault() {
    return_addresses[0] = reinterpret_cast<std::uintptr_t>(PROBE_RETURN_ADDRESS());
    *target = 1;
}

KOTATSU_NOINLINE void probe_callee() {
    calls = calls + 1;
}

// Faults after a call, for which it made its frame record.
KOTATSU_NOINLINE void probe_fault_after_call() {
    return_addresses[0] = reinterpret_cast<std::uintptr_t>(PROBE_RETURN_ADDRESS());
    probe_callee();
    *target = 1;
}

// What probe_inlined calls.
void (*volatile faulting)() = probe_fault;

PROBE_ALWAYS_INLINE void probe_inlined() {
    faulting();
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

#if KOTATSU_ENABLE_EXCEPTIONS
KOTATSU_NOINLINE void probe_throw() {
    throw std::runtime_error("none catches this");
}
#endif

KOTATSU_NOINLINE void probe_trap() {
    return_addresses[0] = reinterpret_cast<std::uintptr_t>(PROBE_RETURN_ADDRESS());
#if KOTATSU_COMPILER_MSVC
    __ud2();
#else
    __builtin_trap();
#endif
}

KOTATSU_NOINLINE void probe_breakpoint() {
    return_addresses[0] = reinterpret_cast<std::uintptr_t>(PROBE_RETURN_ADDRESS());
#if KOTATSU_COMPILER_MSVC
    __debugbreak();
#elif defined(__aarch64__)
    asm volatile("brk #0xf000");
#else
    asm volatile("int3");
#endif
    calls = calls + 1;
}

int probe_recurse_a(int depth);
int probe_recurse_b(int depth);

// What each calls the other through, which keeps the calls from becoming a
// loop.
int (*volatile recurse_a)(int) = probe_recurse_a;
int (*volatile recurse_b)(int) = probe_recurse_b;

// A KiB of stack a call, until it overflows; without ASan's calls into its
// runtime, where the overflow would fault otherwise.
PROBE_NO_ASAN KOTATSU_NOINLINE int probe_recurse_a(int depth) {
    volatile char frame[1024];
    frame[0] = static_cast<char>(depth);
    // Never taken: a recursion without an end may be assumed not to happen.
    if(depth < 0) {
        return 0;
    }
    return recurse_b(depth + 1) + frame[0];
}

PROBE_NO_ASAN KOTATSU_NOINLINE int probe_recurse_b(int depth) {
    volatile char frame[1024];
    frame[0] = static_cast<char>(depth);
    if(depth < 0) {
        return 0;
    }
    return recurse_a(depth + 1) + frame[0];
}

// The threads of crash_two that are about to fault.
std::atomic<int> about_to_fault = 0;

// Faults once the other thread of crash_two is about to.
KOTATSU_NOINLINE void probe_fault_together() {
    about_to_fault.fetch_add(1);
    while(about_to_fault.load() < 2) {}
    *target = 1;
}

// What the handler of crash_in_handler and of crash_two does.
volatile bool fault_in_handler = false;
volatile bool wait_for_both = false;

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

/// A line of JSON written to stdout, built without allocating, in a
/// handler.
struct Line {
    char text[128];
    std::size_t size = 0;

    Line& put(std::string_view part) {
        for(char c: part) {
            text[size++] = c;
        }
        return *this;
    }

    /// `value` as a string of 0x and `digits` hexadecimal digits.
    Line& hex(std::uint64_t value, int digits) {
        put(R"("0x)");
        for(int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
            text[size++] = "0123456789abcdef"[(value >> shift) & 0xF];
        }
        return put(R"(")");
    }

    Line& decimal(unsigned value) {
        char digits[12];
        int count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while(value != 0);
        while(count > 0) {
            text[size++] = digits[--count];
        }
        return *this;
    }

    /// Writes the line, closing its object.
    void write() {
        put("}\n");
        kotatsu::sys::write_console(1, std::string_view(text, size));
    }
};

void print_fault(std::uintptr_t address) {
    Line().put(R"({"fault":)").hex(address, 16).write();
}

/// {"signal": <crash's>}, on Windows with "code" for an exception.
void print_crash(const kotatsu::sys::Crash& crash) {
    Line line;
    line.put(R"({"signal":)").decimal(static_cast<unsigned>(crash.signal));
#ifdef _WIN32
    if(crash.info != nullptr) {
        line.put(R"(,"code":)")
            .hex(static_cast<const EXCEPTION_RECORD*>(crash.info)->ExceptionCode, 8);
    }
#endif
    line.write();
}

/// The recorded return addresses.
void print_returns() {
    Line line;
    line.put(R"({"returns":[)");
    bool first = true;
    for(auto address: return_addresses) {
        if(address == 0) {
            continue;
        }
        if(!first) {
            line.put(",");
        }
        first = false;
        line.hex(address, 16);
    }
    line.put("]").write();
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

// on_crash's handler.
void reported(const kotatsu::sys::Crash& crash) noexcept {
    print_crash(crash);
    if(fault_in_handler) {
        *target = 2;
    }
    while(wait_for_both && about_to_fault.load() < 2) {}
#ifdef _WIN32
    if(crash.info != nullptr) {
        print_fault(reinterpret_cast<std::uintptr_t>(
            static_cast<const EXCEPTION_RECORD*>(crash.info)->ExceptionAddress));
    }
#else
    print_fault(kotatsu::test::interrupted_at(crash.context));
#endif
    print_returns();
    kotatsu::sys::write_stacktrace(1, crash);
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

void on_fault(bool) {
    ::SetUnhandledExceptionFilter(filtered);
}

// The filter crash_after_handler sets before on_crash.
LONG WINAPI previously(EXCEPTION_POINTERS* exception) {
    Line()
        .put(R"({"previous":)")
        .hex(reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress), 16)
        .write();
    std::_Exit(4);
}

void before_crash_handler() {
    ::SetUnhandledExceptionFilter(previously);
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
    print_fault(kotatsu::test::interrupted_at(context));
    end();
}

void aborted(int, siginfo_t*, void*) {
    end();
}

void handle(int signal, void (*handler)(int, siginfo_t*, void*), int flags = SA_ONSTACK) {
    struct sigaction action = {};
    action.sa_sigaction = handler;
    action.sa_flags = SA_SIGINFO | flags;
    sigemptyset(&action.sa_mask);
    sigaction(signal, &action, nullptr);
}

/// With `alternate`, on an alternate stack, as a crash handler sets one for
/// a stack that overflowed; else on the thread's own.
void on_fault(bool alternate) {
    if(alternate) {
        static char room[256 * 1024];
        stack_t stack = {};
        stack.ss_sp = room;
        stack.ss_size = sizeof(room);
        sigaltstack(&stack, nullptr);
    }
    handle(SIGSEGV, faulted, alternate ? SA_ONSTACK : 0);
    handle(SIGBUS, faulted, alternate ? SA_ONSTACK : 0);
}

void on_abort() {
    handle(SIGABRT, aborted);
}

// The handler crash_after_handler sets before on_crash.
void previously(int, siginfo_t*, void* context) {
    Line().put(R"({"previous":)").hex(kotatsu::test::interrupted_at(context), 16).write();
    std::_Exit(4);
}

void before_crash_handler() {
    handle(SIGSEGV, previously, 0);
}

#ifndef PROBE_TSAN
/// SIGTRAP's default action, with SA_SIGINFO, as macOS may report the
/// default of a process.
void default_with_siginfo() {
    struct sigaction action = {};
    action.sa_handler = SIG_DFL;
    action.sa_flags = SA_SIGINFO;
    sigaction(SIGTRAP, &action, nullptr);
}
#endif

#endif

void print_image() {
    auto image = executable();
#ifdef _MSC_VER
    constexpr std::string_view debug = "pdb";
#else
    constexpr std::string_view debug = "dwarf";
#endif
#ifdef PROBE_TSAN
    constexpr bool tsan = true;
#else
    constexpr bool tsan = false;
#endif
    constexpr bool exceptions = KOTATSU_ENABLE_EXCEPTIONS;
    print_line(std::format(
        R"({{"executable":{},"base":"{:#x}","low":"{:#x}","high":"{:#x}","debug":{},"tsan":{},"exceptions":{}}})",
        json(image.path),
        image.base,
        image.low,
        image.high,
        json(debug),
        tsan,
        exceptions));
}

#ifdef __linux__
struct Early {
    Early() {
        if(const char* value = std::getenv("KOTATSU_PROBE_EARLY");
           value != nullptr && std::string_view(value) == "1") {
            on_fault(true);
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
    if(command == "fault" || command == "fault_on_stack" || command == "fault_after_call") {
        if(command == "fault_after_call") {
            faulting = probe_fault_after_call;
        }
        on_fault(command != "fault_on_stack");
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
        on_fault(true);
        print_image();
        probe_call();
        return 1;
    }
    if(command.starts_with("crash_")) {
#ifndef _WIN32
        if(command == "crash_ignored") {
            std::signal(SIGTRAP, SIG_IGN);
        }
#ifndef PROBE_TSAN
        // TSan takes such an action for a handler of its own, which drops the
        // signal.
        if(command == "crash_breakpoint") {
            default_with_siginfo();
        }
#endif
#endif
        if(command == "crash_after_handler") {
            before_crash_handler();
        }
        if(auto error = kotatsu::sys::on_crash(reported)) {
            std::fprintf(stderr, "[error] on_crash: %s\n", error.message().c_str());
            return 1;
        }
        print_image();
        auto on_threads = [](auto... fns) {
            kotatsu::sys::Thread threads[] = {
                kotatsu::sys::Thread::start(1U << 20, fns).value()...};
            for(auto& thread: threads) {
                thread.join();
            }
        };
        if(command == "crash_fault" || command == "crash_after_handler") {
            probe_caller();
        } else if(command == "crash_trap") {
            probe_trap();
        } else if(command == "crash_breakpoint") {
            probe_breakpoint();
        } else if(command == "crash_abort") {
            probe_abort();
        } else if(command == "crash_throw") {
#if KOTATSU_ENABLE_EXCEPTIONS
            probe_throw();
#else
            return 0;
#endif
        } else if(command == "crash_overflow") {
            calls = probe_recurse_a(0);
        } else if(command == "crash_overflow_on_thread") {
            on_threads([] { calls = probe_recurse_a(0); });
        } else if(command == "crash_two") {
            wait_for_both = true;
            on_threads(probe_fault_together, probe_fault_together);
#ifndef _WIN32
        } else if(command == "crash_in_handler") {
            fault_in_handler = true;
            probe_caller();
        } else if(command == "crash_raise") {
            std::raise(SIGSEGV);
        } else if(command == "crash_terminate") {
            std::raise(SIGTERM);
        } else if(command == "crash_ignored") {
            std::raise(SIGTRAP);
            std::_Exit(5);
#endif
        } else {
            std::fprintf(stderr, "[error] unknown command\n");
            return 2;
        }
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
