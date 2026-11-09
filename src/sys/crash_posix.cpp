#ifndef _WIN32

#include <algorithm>
#include <atomic>
#include <cassert>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include "os.h"
#include "kotatsu/sys/crash.h"

namespace kotatsu::sys {

namespace {

constexpr int crash_signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGFPE, SIGABRT, SIGSYS};

/// The handler on_crash was given: null until it is called.
constinit std::atomic<CrashHandler> callback = nullptr;

/// The actions of crash_signals before on_crash set its own.
constinit struct sigaction previous[std::size(crash_signals)] = {};

/// The thread whose crash is being handled, by its pthread_t: 0 for none.
constinit std::atomic<std::uintptr_t> crashing = 0;

/// The calling thread's pthread_t, an integer on Linux and a pointer on
/// macOS, never 0.
std::uintptr_t this_thread() noexcept {
    auto self = ::pthread_self();
    std::uintptr_t id = 0;
    std::memcpy(&id, &self, sizeof(self));
    return id;
}

/// Whether a process sent the signal, by kill, raise or abort, rather than
/// a fault of the thread's raising it: Linux numbers SI_USER, SI_QUEUE,
/// SI_TKILL and the other senders' codes from 0 down; macOS has SI_USER and
/// SI_QUEUE of its own, and 0 for what pthread_kill sends.
bool sent(const siginfo_t& info) noexcept {
    return info.si_code <= 0 || info.si_code == SI_USER || info.si_code == SI_QUEUE;
}

/// Puts back the actions on_crash replaced, the default for one that was
/// ignored, against which a fault run again would loop.
void restore() noexcept {
    for(std::size_t index = 0; index < std::size(crash_signals); ++index) {
        auto action = previous[index];
        if(!(action.sa_flags & SA_SIGINFO) && action.sa_handler == SIG_IGN) {
            action.sa_handler = SIG_DFL;
        }
        ::sigaction(crash_signals[index], &action, nullptr);
    }
}

void crashed(int signal, siginfo_t* info, void* context) noexcept {
    const auto self = this_thread();
    std::uintptr_t first = 0;
    if(crashing.compare_exchange_strong(first, self, std::memory_order_acq_rel)) {
        callback.load(std::memory_order_acquire)(
            Crash{.signal = signal, .info = info, .context = context});
    } else if(first != self) {
        // Another thread's crash, whose handler ends the process.
        while(true) {
            ::pause();
        }
    }
    // Here too after a crash in the handler, which the signal, left
    // unblocked, brought back.
    restore();
    // A fault of the thread's own comes again as the handler returns, to the
    // action put back; anything else is raised again, at once, unblocked as
    // it is.
    const bool fault =
        signal == SIGSEGV || signal == SIGBUS || signal == SIGILL || signal == SIGFPE;
    if(!fault || sent(*info)) {
        ::raise(signal);
    }
}

constexpr std::size_t crash_stack_size = 64 * 1024;

/// The calling thread's crash stack, and the alternate stack it had before,
/// put back as the thread ends while the crash stack is still the one set.
struct CrashStack {
    bool reserved = false;
    /// The crash stack's mapping, a guard page below it: null for a thread
    /// that kept its own.
    void* mapping = nullptr;
    std::size_t mapped = 0;
    stack_t before = {};

    CrashStack() = default;
    CrashStack(const CrashStack&) = delete;
    CrashStack& operator=(const CrashStack&) = delete;

    ~CrashStack() {
        if(mapping == nullptr) {
            return;
        }
        auto* ours = static_cast<char*>(mapping) + (mapped - crash_stack_size);
        if(stack_t current; ::sigaltstack(nullptr, &current) == 0 && current.ss_sp == ours) {
            // macOS checks the size even of a stack it is to disable.
            before.ss_size = std::max<std::size_t>(before.ss_size, MINSIGSTKSZ);
            ::sigaltstack(&before, nullptr);
        }
        ::munmap(mapping, mapped);
    }
};

thread_local CrashStack crash_stack;

}  // namespace

std::error_code on_crash(CrashHandler handler) noexcept {
    assert(handler != nullptr);
    if(callback.exchange(handler, std::memory_order_acq_rel) == nullptr) {
        struct sigaction action = {};
        action.sa_sigaction = crashed;
        // On the crash stack, and with the signal left unblocked: a crash in
        // the handler comes back to it rather than waiting on itself.
        action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
        sigemptyset(&action.sa_mask);
        for(std::size_t index = 0; index < std::size(crash_signals); ++index) {
            ::sigaction(crash_signals[index], &action, &previous[index]);
        }
    }
    return reserve_crash_stack();
}

std::error_code reserve_crash_stack() noexcept {
    auto& stack = crash_stack;
    if(stack.reserved) {
        return {};
    }
    if(stack_t current; ::sigaltstack(nullptr, &current) == 0 && !(current.ss_flags & SS_DISABLE) &&
                        current.ss_size >= crash_stack_size) {
        stack.reserved = true;
        return {};
    }
    const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    const auto mapped = page + crash_stack_size;
    auto* mapping = ::mmap(nullptr, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if(mapping == MAP_FAILED) {
        return errno_code();
    }
    // A handler that overflows the crash stack faults on the guard page
    // rather than writing past it.
    ::mprotect(mapping, page, PROT_NONE);
    stack_t crash = {};
    crash.ss_sp = static_cast<char*>(mapping) + page;
    crash.ss_size = crash_stack_size;
    // Fails only for a stack smaller than MINSIGSTKSZ, or on the alternate
    // stack, in a signal handler.
    ::sigaltstack(&crash, &stack.before);
    stack.reserved = true;
    stack.mapping = mapping;
    stack.mapped = mapped;
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
