#ifndef _WIN32

#include "signals.h"

#include <csignal>

#include "kota/sys/detail/signals.h"

namespace kota::sys {

constinit std::atomic<bool> sigpipe_was_default = false;

constinit std::atomic<bool> sigchld_was_ignored = false;

#ifdef __APPLE__
constinit std::mutex spawning;
#endif

void stop_auto_reaping() noexcept {
    struct sigaction action = {};
    ::sigaction(SIGCHLD, nullptr, &action);
    const bool ignored = action.sa_handler == SIG_IGN;
    if(!ignored && !(action.sa_flags & SA_NOCLDWAIT)) {
        return;
    }
    if(ignored) {
        // Before the action changes: a child started meanwhile that sees
        // it changed sees this too.
        sigchld_was_ignored = true;
        action.sa_handler = SIG_DFL;
    }
    action.sa_flags &= ~SA_NOCLDWAIT;
    ::sigaction(SIGCHLD, &action, nullptr);
}

namespace detail {

void ignore_sigpipe() noexcept {
#ifdef __APPLE__
    std::lock_guard lock(spawning);
#endif
    struct sigaction action = {};
    ::sigaction(SIGPIPE, nullptr, &action);
    if(action.sa_handler == SIG_DFL) {
        // Before the action changes, as stop_auto_reaping's.
        sigpipe_was_default = true;
        action.sa_handler = SIG_IGN;
        ::sigaction(SIGPIPE, &action, nullptr);
    }
}

}  // namespace detail

}  // namespace kota::sys

#endif
