#pragma once

#include <string_view>
#include <system_error>

namespace kota::sys {

/// Writes `text`, UTF-8, to the open file descriptor `fd`, all of it: to a
/// Windows console as UTF-16 through WriteConsoleW, each ill-formed sequence
/// as U+FFFD, since bytes would show in the console's code page; anywhere
/// else, a pipe, a file or a POSIX terminal, as the bytes they are.
/// Allocates nothing and never throws; the error of the write that failed.
/// A pipe that is full and does not block (O_NONBLOCK, Windows' PIPE_NOWAIT)
/// fails with resource_unavailable_try_again, part of `text` possibly
/// written. On Windows a descriptor that is not open ends the process, as
/// the CRT's check of it does.
std::error_code write_console(int fd, std::string_view text) noexcept;

}  // namespace kota::sys
