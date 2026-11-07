#pragma once

#include <cstddef>
#include <expected>
#include <string_view>
#include <system_error>

// What a descriptor is, and writing text to it. On Windows a descriptor that
// is not open does not end the process, as the C runtime's own check of it
// would; MSVC's debug runtime (/MDd, /MTd) still reports that check's failed
// assertion first, in a dialog that waits for an answer unless
// _CrtSetReportMode sends its reports elsewhere.

namespace kotatsu::sys {

/// Writes `text`, UTF-8, to the open file descriptor `fd`, all of it: to a
/// Windows console as UTF-16 through WriteConsoleW, each ill-formed sequence
/// as U+FFFD, since bytes would show in the console's code page; anywhere
/// else, a pipe, a file or a POSIX terminal, as the bytes they are.
/// Allocates nothing and never throws; on POSIX it is async-signal-safe.
/// The error of the write that failed, bad_file_descriptor for a descriptor
/// not open. A pipe that is full and does not block (O_NONBLOCK, Windows'
/// PIPE_NOWAIT) fails with resource_unavailable_try_again, part of `text`
/// possibly written: write_console_some() is the way to count it.
std::error_code write_console(int fd, std::string_view text) noexcept;

/// Writes the start of `text` as write_console() writes it, in one write:
/// how many bytes of it, at least one, or the error of a write that took
/// none: resource_unavailable_try_again from a pipe that does not block and
/// has no room for them, which on Windows may take none of a write larger
/// than the room it has; io_error from a device that took none and said no
/// error. To a Windows console, as many whole characters as a buffer of
/// 2048 UTF-16 units holds. 0 for an empty `text`, which it does not write.
/// On POSIX it is async-signal-safe.
std::expected<std::size_t, std::error_code> write_console_some(int fd,
                                                               std::string_view text) noexcept;

/// Makes the writes and reads of the pipe or file descriptor `fd` return at
/// once rather than wait, taking what fits and giving what is there: on
/// POSIX O_NONBLOCK, of the open file, which the descriptors that share it,
/// a child's among them, see too; on Windows PIPE_NOWAIT, its read mode
/// kept, of a pipe alone, whose write of more than the room it has may take
/// nothing. Windows: device_or_resource_busy for a pipe Windows says is
/// busy (ERROR_PIPE_BUSY), which a later call may get past; not_supported
/// for a descriptor that is no pipe. bad_file_descriptor for a descriptor
/// not open.
std::error_code set_nonblocking(int fd) noexcept;

/// Whether descriptor `fd` is a terminal: on Windows a console, whose mode
/// GetConsoleMode reads; on POSIX one isatty takes. False for a descriptor
/// not open.
bool is_console(int fd) noexcept;

/// Whether descriptor `fd` is a pipe or a socket, a FIFO included; on
/// Windows one GetFileType calls a pipe, as it does a socket. False for a
/// descriptor not open.
bool is_pipe(int fd) noexcept;

}  // namespace kotatsu::sys
