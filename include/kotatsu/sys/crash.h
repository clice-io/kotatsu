#pragma once

#include <system_error>

// A program's crash handler: on_crash, which calls a function as the process
// crashes and then lets it end by the crash, and the alternate stack that
// lets it run on a thread whose stack overflowed.

namespace kotatsu::sys {

/// A crash, as on_crash's callback is told of it.
struct Crash {
    /// The signal: SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGFPE, SIGABRT or
    /// SIGSYS. On Windows SIGABRT for abort(), and for an exception SIGSEGV,
    /// SIGILL or SIGFPE by the fault it reports, 0 for any other, as a C++
    /// exception none caught.
    int signal = 0;
    /// The signal's siginfo_t, or on Windows the EXCEPTION_RECORD; null for
    /// SIGABRT on Windows.
    const void* info = nullptr;
    /// The context the signal interrupted, a ucontext_t, or on Windows the
    /// exception's CONTEXT, and for SIGABRT the CONTEXT of raise(), which
    /// called its handler.
    const void* context = nullptr;
};

/// What on_crash calls. It runs in a signal handler, or in Windows'
/// unhandled exception filter or SIGABRT handler, and may call only
/// async-signal-safe functions, as write_console and write_stacktrace.
using CrashHandler = void (*)(const Crash& crash) noexcept;

/// Calls `handler` as the process crashes: on a SIGSEGV, SIGBUS, SIGILL,
/// SIGTRAP, SIGFPE, SIGABRT or SIGSYS, but one the program ignores, never on
/// SIGINT, SIGTERM or SIGHUP; on Windows on an exception nothing handled and
/// on abort()'s SIGABRT. A later call replaces `handler`; the handlers are
/// set up once. Gives the calling thread a crash stack
/// (reserve_crash_stack), each thread Thread starts after it its own as it
/// starts, and each thread of libuv's pool its own as it runs a function of
/// async::queue().
///
/// One crash at a time: a thread that crashes while another's handler runs
/// waits for the process to end, and a crash in `handler` goes on as if
/// `handler` had returned. Then the actions before on_crash are put back,
/// and the signal goes to the one it had: the default, raised again, ends
/// the process by the signal, as the parent sees it; a handler of the
/// program's is called with the siginfo and context the signal brought. On
/// Windows the exception goes to the filter set before; when that declines
/// it, or there is none, the process ends at once by TerminateProcess with
/// the exception's code, never going on to Windows Error Reporting, which
/// may hold it for a dialog or a dump first. SIGABRT goes to its handler
/// before, or to abort(), which on_crash keeps from failing fast: it ends
/// the process with 3, std::terminate's abort() too. Both are this
/// process's alone: the processes it starts keep the system's handling.
///
/// A typical handler writes a line with write_console, then the stack with
/// write_stacktrace(fd, crash).
///
/// Set it up from main, before other threads start: the error of the crash
/// stack.
std::error_code on_crash(CrashHandler handler) noexcept;

/// Gives the calling thread room for a crash handler to run in after its
/// stack overflowed, once: on Linux and macOS an alternate signal stack of
/// 64 KiB, unless it has one at least that large, which it keeps, the one
/// it had put back as the thread ends; on Windows a stack guarantee of
/// 64 KiB, as SetThreadStackGuarantee gives. The system's error when it
/// cannot: not_enough_memory, or on Linux and macOS operation_not_permitted
/// in a signal handler that runs on the alternate stack.
std::error_code reserve_crash_stack() noexcept;

namespace detail {

/// reserve_crash_stack() once on_crash was called: what the threads Thread
/// starts and those async runs work on call.
void reserve_crash_stack_if_handled() noexcept;

}  // namespace detail

}  // namespace kotatsu::sys
