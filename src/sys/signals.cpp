#ifndef _WIN32

#include "signals.h"

#include <csignal>

#include "kota/sys/detail/signals.h"

namespace kota::sys {

constinit std::atomic<bool> ignored_sigpipe = false;

constinit std::atomic<bool> restored_sigchld = false;

void keep_children_waitable() noexcept {
    struct sigaction action = {};
    if(::sigaction(SIGCHLD, nullptr, &action) != 0) {
        return;
    }
    const bool ignored = action.sa_handler == SIG_IGN;
    if(!ignored && !(action.sa_flags & SA_NOCLDWAIT)) {
        return;
    }
    if(ignored) {
        action.sa_handler = SIG_DFL;
        restored_sigchld = true;
    }
    action.sa_flags &= ~SA_NOCLDWAIT;
    ::sigaction(SIGCHLD, &action, nullptr);
}

namespace detail {

void ignore_sigpipe() noexcept {
    struct sigaction action = {};
    if(::sigaction(SIGPIPE, nullptr, &action) == 0 && action.sa_handler == SIG_DFL) {
        action.sa_handler = SIG_IGN;
        ::sigaction(SIGPIPE, &action, nullptr);
        ignored_sigpipe = true;
    }
}

}  // namespace detail

}  // namespace kota::sys

#endif
