#include "kotatsu/sys/crash.h"

#include <atomic>
#include <cassert>
#include <csignal>
#include <cstddef>
#include <cstdint>

#include "os.h"

#ifndef _WIN32
#include <algorithm>
#include <cstring>
#include <iterator>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace kotatsu::sys {

namespace {

/// The handler on_crash was given: null until it is called.
constinit std::atomic<CrashHandler> callback = nullptr;

/// The room a crash stack gives a handler.
constexpr std::size_t crash_stack_size = 64 * 1024;

#ifdef _WIN32

using ThreadID = DWORD;

ThreadID this_thread() noexcept {
    return ::GetCurrentThreadId();
}

[[noreturn]] void wait_for_the_end() noexcept {
    while(true) {
        ::Sleep(INFINITE);
    }
}

#else

/// A pthread_t, an integer on Linux and a pointer on macOS.
using ThreadID = std::uintptr_t;

ThreadID this_thread() noexcept {
    auto self = ::pthread_self();
    ThreadID id = 0;
    std::memcpy(&id, &self, sizeof(self));
    return id;
}

[[noreturn]] void wait_for_the_end() noexcept {
    while(true) {
        ::pause();
    }
}

#endif

/// The thread whose crash is being handled: 0 for none, which no thread is.
constinit std::atomic<ThreadID> crashing = 0;

/// Calls the handler for `crash` when the calling thread is the first to
/// crash. A thread that crashes after another waits for the other's crash
/// to end the process; the first, crashing again in the handler, goes on
/// without it.
void report(const Crash& crash) noexcept {
    const auto self = this_thread();
    ThreadID first = 0;
    if(crashing.compare_exchange_strong(first, self, std::memory_order_acq_rel)) {
        callback.load(std::memory_order_acquire)(crash);
    } else if(first != self) {
        wait_for_the_end();
    }
}

#ifdef _WIN32

using SignalHandler = void (*)(int);

/// The unhandled exception filter and SIGABRT's handler before on_crash set
/// its own.
constinit LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = nullptr;
constinit SignalHandler previous_abort = nullptr;

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

void restore() noexcept {
    ::SetUnhandledExceptionFilter(previous_filter);
    std::signal(SIGABRT, previous_abort);
}

LONG WINAPI filtered(EXCEPTION_POINTERS* exception) {
    report(Crash{.signal = signal_of(exception->ExceptionRecord->ExceptionCode),
                 .info = exception->ExceptionRecord,
                 .context = exception->ContextRecord});
    restore();
    return previous_filter != nullptr ? previous_filter(exception) : EXCEPTION_CONTINUE_SEARCH;
}

void aborted(int signal) {
    report(Crash{.signal = signal});
    restore();
    if(previous_abort != SIG_DFL) {
        previous_abort(signal);
        return;
    }
    // raise() set the default back as it called this, without which abort()
    // ends the process, from abort() itself as from raise(SIGABRT).
    std::abort();
}

void install() noexcept {
    previous_filter = ::SetUnhandledExceptionFilter(filtered);
    // Without a handler, abort() fails fast past every filter.
    previous_abort = std::signal(SIGABRT, aborted);
    if(previous_abort == SIG_IGN) {
        std::signal(SIGABRT, SIG_IGN);
    }
}

#else

constexpr int crash_signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGFPE, SIGABRT, SIGSYS};

/// The actions of crash_signals before on_crash's.
constinit struct sigaction previous[std::size(crash_signals)] = {};

void crashed(int signal, siginfo_t* info, void* context) noexcept {
    report(Crash{.signal = signal, .info = info, .context = context});
    // Here too after a crash in the handler, which the signal, left
    // unblocked, brought back. The actions before take over, and the signal
    // goes to the one it had: the default, raised again, ends the process by
    // it; a handler of the program's is given what the signal brought.
    struct sigaction before = {};
    for(std::size_t index = 0; index < std::size(crash_signals); ++index) {
        ::sigaction(crash_signals[index], &previous[index], nullptr);
        if(crash_signals[index] == signal) {
            before = previous[index];
        }
    }
    if(before.sa_flags & SA_SIGINFO) {
        before.sa_sigaction(signal, info, context);
    } else if(before.sa_handler == SIG_DFL) {
        ::raise(signal);
    } else {
        before.sa_handler(signal);
    }
}

void install() noexcept {
    struct sigaction action = {};
    action.sa_sigaction = crashed;
    // On the crash stack, and with the signal left unblocked: a crash in the
    // handler comes back to it rather than waiting on itself.
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&action.sa_mask);
    for(std::size_t index = 0; index < std::size(crash_signals); ++index) {
        auto& before = previous[index];
        ::sigaction(crash_signals[index], nullptr, &before);
        // A signal the program ignores stays ignored.
        if((before.sa_flags & SA_SIGINFO) || before.sa_handler != SIG_IGN) {
            ::sigaction(crash_signals[index], &action, nullptr);
        }
    }
}

/// The calling thread's crash stack, and the alternate stack it had before,
/// put back as the thread ends while the crash stack is still the one set.
struct CrashStack {
    bool reserved = false;
    /// The crash stack's mapping, a guard page below it: null for a thread
    /// that kept its own.
    void* mapping = nullptr;
    std::size_t mapped = 0;
    void* stack = nullptr;
    stack_t before = {};

    CrashStack() = default;
    CrashStack(const CrashStack&) = delete;
    CrashStack& operator=(const CrashStack&) = delete;

    ~CrashStack() {
        if(mapping == nullptr) {
            return;
        }
        stack_t current;
        ::sigaltstack(nullptr, &current);
        if(current.ss_sp == stack) {
            // macOS checks the size even of a stack it is to disable.
            before.ss_size = std::max<std::size_t>(before.ss_size, MINSIGSTKSZ);
            ::sigaltstack(&before, nullptr);
        }
        ::munmap(mapping, mapped);
    }
};

thread_local CrashStack crash_stack;

#endif

}  // namespace

std::error_code on_crash(CrashHandler handler) noexcept {
    assert(handler != nullptr);
    if(callback.exchange(handler, std::memory_order_acq_rel) == nullptr) {
        install();
    }
    return reserve_crash_stack();
}

#ifdef _WIN32

std::error_code reserve_crash_stack() noexcept {
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

#else

std::error_code reserve_crash_stack() noexcept {
    auto& reserved = crash_stack;
    if(reserved.reserved) {
        return {};
    }
    stack_t current;
    ::sigaltstack(nullptr, &current);
    if(!(current.ss_flags & SS_DISABLE) && current.ss_size >= crash_stack_size) {
        reserved.reserved = true;
        return {};
    }
    const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    const auto mapped = page + crash_stack_size;
    auto* mapping = ::mmap(nullptr, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if(mapping == MAP_FAILED) {
        return errno_code();
    }
    // A handler that overflows the crash stack faults on the guard page
    // rather than writing past it; without it, as when the process has
    // mapped all it may, the stack is still there.
    ::mprotect(mapping, page, PROT_NONE);
    stack_t crash = {};
    crash.ss_sp = static_cast<char*>(mapping) + page;
    crash.ss_size = crash_stack_size;
    // EPERM on the alternate stack, in a signal handler that runs on it.
    if(::sigaltstack(&crash, &reserved.before) != 0) {
        auto error = errno_code();
        ::munmap(mapping, mapped);
        return error;
    }
    reserved.reserved = true;
    reserved.mapping = mapping;
    reserved.mapped = mapped;
    reserved.stack = crash.ss_sp;
    return {};
}

#endif

void detail::reserve_crash_stack_if_handled() noexcept {
    if(callback.load(std::memory_order_acquire) != nullptr) {
        // A thread that gets none is left without, as it would be anyway.
        [[maybe_unused]] auto error = reserve_crash_stack();
    }
}

}  // namespace kotatsu::sys
