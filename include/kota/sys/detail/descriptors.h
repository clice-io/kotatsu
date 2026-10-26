#pragma once

#ifndef _WIN32

namespace kota::sys::detail {

/// Close-on-exec descriptor `fd`, one kotatsu keeps for itself, moved above
/// the standard streams' numbers when it took one: in a process that closed
/// a standard stream, a new descriptor takes its number, where the process's
/// own reads and writes of the stream reach it and a child inheriting the
/// stream gets it. `fd` itself when it is above them, or negative; moved, a
/// copy, close-on-exec too, and `fd` closed; -1, errno set and `fd` closed,
/// when it cannot be moved.
int above_standard_streams(int fd) noexcept;

}  // namespace kota::sys::detail

#endif
