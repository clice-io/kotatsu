#pragma once

#include <string_view>
#include <system_error>

namespace kota::sys {

/// Writes `text`, UTF-8, to the open file descriptor `fd`, all of it: to a
/// Windows console as UTF-16 through WriteConsoleW, each ill-formed sequence
/// as U+FFFD, since bytes would show in the console's code page; anywhere
/// else, a pipe, a file or a POSIX terminal, as the bytes they are.
/// Allocates nothing and never throws; the error of the write that failed,
/// bad_file_descriptor for a descriptor not open. A pipe that is full and
/// does not block (O_NONBLOCK, Windows' PIPE_NOWAIT) fails with
/// resource_unavailable_try_again, part of `text` possibly written.
std::error_code write_console(int fd, std::string_view text) noexcept;

/// Whether descriptor `fd` is a terminal: on Windows a console, whose mode
/// GetConsoleMode reads; on POSIX one isatty takes. False for a descriptor
/// not open.
bool is_console(int fd) noexcept;

/// Whether descriptor `fd` is a pipe or a socket, a FIFO included; on
/// Windows one GetFileType calls a pipe, as it does a socket. False for a
/// descriptor not open.
bool is_pipe(int fd) noexcept;

}  // namespace kota::sys
