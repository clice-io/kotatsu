#ifndef _WIN32

#include "signals.h"

#include <csignal>

#include "kota/sys/detail/signals.h"

namespace kota::sys {

constinit std::atomic<bool> sigpipe_was_default = false;

constinit std::atomic<bool> sigchld_was_ignored = false;

void stop_auto_reaping() noexcept {
    struct sigaction action = {};
    ::sigaction(SIGCHLD, nullptr, &action);
    const bool ignored = action.sa_handler == SIG_IGN;
    if(!ignored && !(action.sa_flags & SA_NOCLDWAIT)) {
        return;
    }
    if(ignored) {
        action.sa_handler = SIG_DFL;
        sigchld_was_ignored = true;
    }
    action.sa_flags &= ~SA_NOCLDWAIT;
    ::sigaction(SIGCHLD, &action, nullptr);
}

namespace detail {

void ignore_sigpipe() noexcept {
    struct sigaction action = {};
    ::sigaction(SIGPIPE, nullptr, &action);
    if(action.sa_handler == SIG_DFL) {
        action.sa_handler = SIG_IGN;
        ::sigaction(SIGPIPE, &action, nullptr);
        sigpipe_was_default = true;
    }
}

}  // namespace detail

}  // namespace kota::sys

#endif
