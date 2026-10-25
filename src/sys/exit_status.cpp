#include <csignal>
#include <cstdint>
#include <string_view>

#include "kota/sys/process.h"
#include "kota/sys/windows.h"

namespace kota::sys {

std::string_view signal_name(int signum) {
    switch(signum) {
        case SIGINT: return "SIGINT";
        case SIGILL: return "SIGILL";
        case SIGABRT: return "SIGABRT";
        case SIGFPE: return "SIGFPE";
        case SIGSEGV: return "SIGSEGV";
        case SIGTERM: return "SIGTERM";
#ifdef _WIN32
        case SIGBREAK: return "SIGBREAK";
        case SIGABRT_COMPAT: return "SIGABRT";
        // POSIX's number, which a child's kill takes.
        case 9: return "SIGKILL";
#else
        case SIGHUP: return "SIGHUP";
        case SIGQUIT: return "SIGQUIT";
        case SIGTRAP: return "SIGTRAP";
        case SIGBUS: return "SIGBUS";
        case SIGKILL: return "SIGKILL";
        case SIGUSR1: return "SIGUSR1";
        case SIGUSR2: return "SIGUSR2";
        case SIGPIPE: return "SIGPIPE";
        case SIGALRM: return "SIGALRM";
        case SIGCHLD: return "SIGCHLD";
        case SIGCONT: return "SIGCONT";
        case SIGSTOP: return "SIGSTOP";
        case SIGTSTP: return "SIGTSTP";
        case SIGTTIN: return "SIGTTIN";
        case SIGTTOU: return "SIGTTOU";
        case SIGURG: return "SIGURG";
        case SIGXCPU: return "SIGXCPU";
        case SIGXFSZ: return "SIGXFSZ";
        case SIGVTALRM: return "SIGVTALRM";
        case SIGPROF: return "SIGPROF";
        case SIGWINCH: return "SIGWINCH";
        case SIGIO: return "SIGIO";
        case SIGSYS: return "SIGSYS";
#endif
#ifdef __linux__
        case SIGSTKFLT: return "SIGSTKFLT";
        case SIGPWR: return "SIGPWR";
#endif
#ifdef __APPLE__
        case SIGEMT: return "SIGEMT";
        case SIGINFO: return "SIGINFO";
#endif
        default: return {};
    }
}

std::string_view windows::ntstatus_name(std::uint32_t status) {
    switch(status) {
        case 0x40000015: return "STATUS_FATAL_APP_EXIT";
        case 0x80000001: return "STATUS_GUARD_PAGE_VIOLATION";
        case 0x80000002: return "STATUS_DATATYPE_MISALIGNMENT";
        case 0x80000003: return "STATUS_BREAKPOINT";
        case 0x80000004: return "STATUS_SINGLE_STEP";
        case 0xC0000005: return "STATUS_ACCESS_VIOLATION";
        case 0xC0000006: return "STATUS_IN_PAGE_ERROR";
        case 0xC0000008: return "STATUS_INVALID_HANDLE";
        case 0xC000000D: return "STATUS_INVALID_PARAMETER";
        case 0xC0000017: return "STATUS_NO_MEMORY";
        case 0xC000001D: return "STATUS_ILLEGAL_INSTRUCTION";
        case 0xC0000025: return "STATUS_NONCONTINUABLE_EXCEPTION";
        case 0xC0000026: return "STATUS_INVALID_DISPOSITION";
        case 0xC000007B: return "STATUS_INVALID_IMAGE_FORMAT";
        case 0xC000008C: return "STATUS_ARRAY_BOUNDS_EXCEEDED";
        case 0xC000008D: return "STATUS_FLOAT_DENORMAL_OPERAND";
        case 0xC000008E: return "STATUS_FLOAT_DIVIDE_BY_ZERO";
        case 0xC000008F: return "STATUS_FLOAT_INEXACT_RESULT";
        case 0xC0000090: return "STATUS_FLOAT_INVALID_OPERATION";
        case 0xC0000091: return "STATUS_FLOAT_OVERFLOW";
        case 0xC0000092: return "STATUS_FLOAT_STACK_CHECK";
        case 0xC0000093: return "STATUS_FLOAT_UNDERFLOW";
        case 0xC0000094: return "STATUS_INTEGER_DIVIDE_BY_ZERO";
        case 0xC0000095: return "STATUS_INTEGER_OVERFLOW";
        case 0xC0000096: return "STATUS_PRIVILEGED_INSTRUCTION";
        case 0xC00000FD: return "STATUS_STACK_OVERFLOW";
        case 0xC0000135: return "STATUS_DLL_NOT_FOUND";
        case 0xC0000138: return "STATUS_ORDINAL_NOT_FOUND";
        case 0xC0000139: return "STATUS_ENTRYPOINT_NOT_FOUND";
        case 0xC000013A: return "STATUS_CONTROL_C_EXIT";
        case 0xC0000142: return "STATUS_DLL_INIT_FAILED";
        case 0xC00002B4: return "STATUS_FLOAT_MULTIPLE_FAULTS";
        case 0xC00002B5: return "STATUS_FLOAT_MULTIPLE_TRAPS";
        case 0xC0000374: return "STATUS_HEAP_CORRUPTION";
        case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN";
        case 0xC0000417: return "STATUS_INVALID_CRUNTIME_PARAMETER";
        case 0xC0000420: return "STATUS_ASSERTION_FAILURE";
        case 0xC0000602: return "STATUS_FAIL_FAST_EXCEPTION";
        default: return {};
    }
}

}  // namespace kota::sys
