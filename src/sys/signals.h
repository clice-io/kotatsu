#pragma once

#ifndef _WIN32

#include <atomic>

namespace kota::sys {

/// SIGPIPE was at its default until kotatsu ignored it: a child started with
/// keep_signals gets the default back.
extern std::atomic<bool> sigpipe_was_default;

/// On Linux, SIGCHLD was ignored until spawn set it to its default: a child
/// started with keep_signals is given it ignored while it stays there, a
/// default this process set itself since among them, which nothing tells
/// apart.
extern std::atomic<bool> sigchld_was_ignored;

/// Sets SIGCHLD to its default when it is ignored, and clears SA_NOCLDWAIT
/// from its action, a handler kept: either has the system reap a child, and
/// its exit status with it, before wait can.
void stop_auto_reaping() noexcept;

}  // namespace kota::sys

#endif
