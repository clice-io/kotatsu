#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// What only Windows has. The functions of strings and numbers are declared on
// every host, for Windows command lines and exit codes read elsewhere; those
// that ask Windows itself only on Windows.

namespace kota::sys::windows {

/// `argument` as a Windows command line writes it, for the C runtime's rules
/// (and split_command_line) to read back: as it is when it is not empty and
/// holds no space, tab, newline, vertical tab or quote; else in quotes, with
/// each quote escaped and the backslashes before a quote or the closing one
/// doubled.
std::string quote_argument(std::string_view argument);

/// The command line of `arguments`, the program first. The program is read
/// by its own rule, in which a backslash escapes nothing: it is quoted when
/// it is empty or holds a space or a tab, and holds no quote, which no
/// Windows file name does. Each argument after it is quoted by
/// quote_argument.
std::string join_command_line(std::span<const std::string> arguments);

/// The arguments of a command line, by the rules of the C runtime since 2008
/// (Rust's parser): the program ends at the first space or tab outside
/// quotes, every quote in it toggling them; after it, spaces and tabs outside
/// quotes separate arguments, 2n backslashes before a quote make n and the
/// quote toggles, 2n+1 make n and a literal quote, two quotes inside quotes
/// make one, and other backslashes are literal. No wildcard is expanded. An
/// empty line has no arguments.
std::vector<std::string> split_command_line(std::string_view line);

/// The name of NTSTATUS `status` ("STATUS_DLL_NOT_FOUND" for 0xC0000135),
/// among those a program ends with when it crashes or cannot start; empty for
/// another.
std::string_view ntstatus_name(std::uint32_t status);

#ifdef _WIN32

/// The directories CreateProcess searches for a program its command line
/// names without a path: the directory of this program's executable, the
/// current directory, the system directory, the 16-bit system directory, the
/// Windows directory, then PATH.
std::expected<std::vector<std::string>, std::error_code> create_process_directories();

/// The file CreateProcess runs for program `name` from `directories`, by its
/// rules: a name with a separator or a drive is a path, looked for nowhere
/// else; `.exe` is added only to a name without an extension of its own; and
/// a Cygwin symlink is the file it is, which CreateProcess cannot start.
/// Errors as find_program's.
std::expected<std::string, std::error_code>
    find_create_process_program(std::string_view name, std::span<const std::string> directories);

#endif

}  // namespace kota::sys::windows
