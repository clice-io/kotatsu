#pragma once

#ifndef _WIN32

namespace kotatsu::sys::detail {

/// Ignores SIGPIPE when its action is the default, as an event loop does so
/// that a write to a peer that went away fails with EPIPE rather than ending
/// the process. A child started with keep_signals gets the default back.
void ignore_sigpipe() noexcept;

}  // namespace kotatsu::sys::detail

#endif
