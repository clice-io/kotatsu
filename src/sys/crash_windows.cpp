#ifdef _WIN32

#include <atomic>
#include <cassert>
#include <csignal>

#include "os.h"
#include "kotatsu/sys/crash.h"

namespace kotatsu::sys {

namespace {

using SignalHandler = void (*)(int);

/// The handler on_crash was given: null until it is called.
constinit std::atomic<CrashHandler> callback = nullptr;

/// The unhandled exception filter and SIGABRT's handler before on_crash set
/// its own.
constinit LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = nullptr;
constinit SignalHandler previous_abort = nullptr;

/// The thread whose crash is being handled, by its ID: 0 for none, which no
/// thread's is.
constinit std::atomic<DWORD> crashing = 0;

/// The signal POSIX raises for the fault exception `code` reports: SIGSEGV,
/// SIGILL or SIGFPE; 0 for another exception.
int signal_of(DWORD code) noexcept {
    switch(code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
        case EXCEPTION_STACK_OVERFLOW: return SIGSEGV;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION: return SIGILL;
        case EXCEPTION_FLT_DENORMAL_OPERAND:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_INEXACT_RESULT:
        case EXCEPTION_FLT_INVALID_OPERATION:
        case EXCEPTION_FLT_OVERFLOW:
        case EXCEPTION_FLT_STACK_CHECK:
        case EXCEPTION_FLT_UNDERFLOW:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_INT_OVERFLOW: return SIGFPE;
        default: return 0;
    }
}

/// Whether the calling thread is the first to crash, which calls the
/// handler; a thread that crashes after another waits for the process to
/// end, and the first, crashing again, calls it no more.
bool first_to_crash() noexcept {
    const auto self = ::GetCurrentThreadId();
    DWORD first = 0;
    if(crashing.compare_exchange_strong(first, self, std::memory_order_acq_rel)) {
        return true;
    }
    while(first != self) {
        ::Sleep(INFINITE);
    }
    return false;
}

void restore() noexcept {
    ::SetUnhandledExceptionFilter(previous_filter);
    std::signal(SIGABRT, previous_abort);
}

LONG WINAPI filtered(EXCEPTION_POINTERS* exception) {
    if(first_to_crash()) {
        callback.load(std::memory_order_acquire)(
            Crash{.signal = signal_of(exception->ExceptionRecord->ExceptionCode),
                  .info = exception->ExceptionRecord,
                  .context = exception->ContextRecord});
    }
    restore();
    return previous_filter != nullptr ? previous_filter(exception) : EXCEPTION_CONTINUE_SEARCH;
}

void aborted(int) {
    if(first_to_crash()) {
        callback.load(std::memory_order_acquire)(Crash{.signal = SIGABRT});
    }
    // raise() set the default back as it called this, and abort() then ends
    // the process as without a handler; one set before is raised to.
    restore();
    if(previous_abort != SIG_DFL && previous_abort != SIG_IGN) {
        std::raise(SIGABRT);
    }
}

}  // namespace

std::error_code on_crash(CrashHandler handler) noexcept {
    assert(handler != nullptr);
    if(callback.exchange(handler, std::memory_order_acq_rel) == nullptr) {
        previous_filter = ::SetUnhandledExceptionFilter(filtered);
        // Without a handler, abort() fails fast past every filter.
        previous_abort = std::signal(SIGABRT, aborted);
    }
    return reserve_crash_stack();
}

std::error_code reserve_crash_stack() noexcept {
    constexpr ULONG crash_stack_size = 64 * 1024;
    // 0 asks for the guarantee the thread has.
    ULONG size = 0;
    ::SetThreadStackGuarantee(&size);
    if(size >= crash_stack_size) {
        return {};
    }
    size = crash_stack_size;
    if(!::SetThreadStackGuarantee(&size)) {
        return fs::windows::last_error();
    }
    return {};
}

void detail::reserve_crash_stack_if_handled() noexcept {
    if(callback.load(std::memory_order_acquire) != nullptr) {
        // A thread that gets none is left without, as it would be anyway.
        [[maybe_unused]] auto error = reserve_crash_stack();
    }
}

}  // namespace kotatsu::sys

#endif
