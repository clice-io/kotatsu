#pragma once

#ifndef _WIN32

#include <atomic>

#ifdef __APPLE__
#include <mutex>
#endif

namespace kotatsu::sys {

/// SIGPIPE was at its default when kotatsu ignored it, noted before it does:
/// a child started with keep_signals gets the default back.
extern std::atomic<bool> sigpipe_was_default;

/// On Linux, SIGCHLD was ignored until spawn set it to its default: a child
/// started with keep_signals is given it ignored while it stays there, a
/// default this process set itself since among them, which nothing tells
/// apart.
extern std::atomic<bool> sigchld_was_ignored;

#ifdef __APPLE__
/// Held while a pipe is made and until posix_spawn has started the child it
/// is for: macOS sets close-on-exec only after it makes one, which another
/// of these spawns would otherwise give its child. Held too while
/// ignore_sigpipe ignores SIGPIPE, so that the child posix_spawn starts
/// reads sigpipe_was_default as it stood with SIGPIPE's action, and while a
/// spawn clears its streams' O_NONBLOCK, so that one that fails puts it back
/// on no stream another child started with.
extern std::mutex spawning;
#endif

/// Sets SIGCHLD to its default when it is ignored, and clears SA_NOCLDWAIT
/// from its action, a handler kept: either has the system reap a child, and
/// its exit status with it, before wait can.
void stop_auto_reaping() noexcept;

}  // namespace kotatsu::sys

#endif
