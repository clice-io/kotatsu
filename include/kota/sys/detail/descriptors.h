#pragma once

#ifndef _WIN32

namespace kota::sys::detail {

/// `fd`, a close-on-exec descriptor kotatsu keeps for itself, moved above 2
/// when it took a standard stream's number: a process that closed the stream
/// gives the number out again, and there the process's own reads and writes
/// of the stream, and a child inheriting it, reach it. Gives `fd` as it is
/// when it is above 2 or negative, errno untouched; else a close-on-exec copy
/// above 2, or -1 with errno set, `fd` closed either way.
int move_above_standard_streams(int fd) noexcept;

}  // namespace kota::sys::detail

#endif
