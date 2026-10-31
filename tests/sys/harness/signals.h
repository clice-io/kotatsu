#pragma once

// ScopedSignalAction and ScopedSignalMask, this process's signal state
// changed until they go.

#ifndef _WIN32

#include <csignal>

#include "kotatsu/zest/zest.h"

namespace kotatsu::test {

/// The action for signal `signum` set to `handler` with `flags` until this
/// goes, which puts back what it was.
struct ScopedSignalAction {
    ScopedSignalAction(int signum, void (*handler)(int), int flags = 0) : signum(signum) {
        struct sigaction action = {};
        action.sa_handler = handler;
        action.sa_flags = flags;
        ZASSERT(::sigaction(signum, &action, &previous) == 0);
    }

    ScopedSignalAction(const ScopedSignalAction&) = delete;
    ScopedSignalAction& operator=(const ScopedSignalAction&) = delete;

    ~ScopedSignalAction() {
        ::sigaction(signum, &previous, nullptr);
    }

    int signum;
    struct sigaction previous = {};
};

/// Signal `signum` blocked in this thread until this goes, which puts back
/// the mask it had.
struct ScopedSignalMask {
    explicit ScopedSignalMask(int signum) {
        sigset_t blocked;
        sigemptyset(&blocked);
        sigaddset(&blocked, signum);
        ZASSERT(::pthread_sigmask(SIG_BLOCK, &blocked, &previous) == 0);
    }

    ScopedSignalMask(const ScopedSignalMask&) = delete;
    ScopedSignalMask& operator=(const ScopedSignalMask&) = delete;

    ~ScopedSignalMask() {
        ::pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    }

    sigset_t previous;
};

/// A handler that does nothing, which is no ignoring: exec puts it back to
/// the default.
inline void do_nothing(int) {}

}  // namespace kotatsu::test

#endif
