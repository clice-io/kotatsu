#pragma once

#ifndef _WIN32

#include <atomic>

namespace kota::sys {

/// kotatsu ignored SIGPIPE, which was at its default: a child started with
/// keep_signals gets the default back while it stays ignored.
extern std::atomic<bool> ignored_sigpipe;

/// spawn put SIGCHLD, which was ignored, back to its default: a child
/// started with keep_signals is given it ignored while it stays at the
/// default.
extern std::atomic<bool> restored_sigchld;

/// Puts SIGCHLD back to its default action when it is ignored, and clears
/// SA_NOCLDWAIT from its action: either has the system reap a child, and
/// its exit status with it, before wait can. libuv's handler of it did as
/// much.
void keep_children_waitable() noexcept;

}  // namespace kota::sys

#endif
