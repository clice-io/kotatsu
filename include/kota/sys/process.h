#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace kota::sys {

/// This process's id.
int pid() noexcept;

/// The id of this process's parent. On POSIX the parent it has now: init or a
/// subreaper once the one that started it has exited. On Windows the process
/// that started it, which may have exited and its id gone to another.
int parent_pid() noexcept;

/// The absolute path of this program's executable: symbolic links resolved
/// on Linux and macOS, as Windows names the module on Windows.
std::expected<std::string, std::error_code> executable_path();

/// The arguments this program was started with, argv[0] first. On POSIX
/// `argv`'s strings. On Windows `argc` and `argv` are left alone, the CRT
/// having decoded them in the ANSI code page: the arguments are split from
/// GetCommandLineW() by split_windows_command_line, in WTF-8, without the
/// wildcard expansion some CRTs and LLVM's tools add.
std::vector<std::string> arguments(int argc, const char* const* argv);

/// `argument` as a Windows command line writes it, for the CRT's rules (and
/// split_windows_command_line) to read back: as it is when it is not empty
/// and holds no space, tab, newline, vertical tab or quote; else in quotes,
/// with each quote escaped and the backslashes before a quote or the closing
/// one doubled.
std::string quote_windows_argument(std::string_view argument);

/// The Windows command line of `arguments`, the program first. The program is
/// read by its own rule, in which a backslash escapes nothing: it is quoted
/// when it is empty or holds a space or a tab, and holds no quote, which no
/// Windows file name does. Each argument after it is quoted by
/// quote_windows_argument.
std::string join_windows_command_line(std::span<const std::string> arguments);

/// The arguments of a Windows command line, by the rules of the CRT since
/// 2008 (Rust's parser): the program ends at the first space or tab outside
/// quotes, every quote in it toggling them; after it, spaces and tabs outside
/// quotes separate arguments, 2n backslashes before a quote make n and the
/// quote toggles, 2n+1 make n and a literal quote, two quotes inside quotes
/// make one, and other backslashes are literal. No wildcard is expanded. An
/// empty line has no arguments.
std::vector<std::string> split_windows_command_line(std::string_view line);

/// Where find_program looks for a program.
struct ProgramSearch {
    /// The directories, in order: PATH's entries when empty, and on POSIX
    /// confstr(_CS_PATH)'s ("/bin:/usr/bin") when PATH is unset. An empty
    /// entry is the current directory on POSIX and nothing on Windows, whose
    /// current directory is searched only when listed, as cmd lists it first.
    std::vector<std::string> directories = {};

#ifdef _WIN32
    /// The extensions a name is tried with, compared ignoring case: when
    /// empty, PATHEXT's (.COM;.EXE;.BAT;.CMD when it is unset or lists none),
    /// or `.exe` like_create_process. A name that has one of them is tried as
    /// it is, another with each of them in turn, in one directory before the
    /// next; "" stands for the name as it is.
    std::vector<std::string> extensions = {};

    /// Names are tried as CreateProcess tries them: `extensions` go only
    /// after a name without an extension of its own, and a Cygwin symlink is
    /// the file it is, which CreateProcess cannot start.
    bool like_create_process = false;
#endif
};

/// The file program `name` names. On POSIX as execvp finds it: `name` itself
/// when it holds a `/`, else the first regular file with execute permission
/// in the directories of `search`. On Windows the first file that is no
/// directory, a name with a separator or a drive being taken as a path
/// rather than looked for; a Cygwin symlink (`/usr/bin/cc`) is followed to
/// the program it names beside it, `.exe` added when the target has none,
/// and counts as a file that cannot be run when it names a POSIX path, which
/// only its runtime's mount table maps. no_such_file_or_directory when
/// nothing is found, permission_denied when only files that cannot be run
/// are.
std::expected<std::string, std::error_code> find_program(std::string_view name,
                                                         const ProgramSearch& search = {});

#ifdef _WIN32
namespace windows {

/// The search CreateProcess makes for a program its command line names
/// without a path, like_create_process: the directory of this program's
/// executable, the current directory, the system directory, the 16-bit
/// system directory, the Windows directory, then PATH.
std::expected<ProgramSearch, std::error_code> create_process_search();

}  // namespace windows
#endif

#ifndef _WIN32
/// Sets the title `ps` shows for this process: the memory of its arguments
/// is overwritten with `title`, cut to their length, and on Linux the name
/// `top` shows, the first 15 bytes of it. The strings of argv then hold the
/// title: take arguments() before. Not thread-safe against readers of argv.
/// Windows has no such title; its console's is shared with the processes
/// before this one.
std::error_code set_process_title(std::string_view title);
#endif

/// The name of signal `signum` on this host ("SIGSEGV"); empty for a number
/// it has no signal by. On Windows the signals of the CRT, and those libuv
/// numbers as POSIX does to kill a process (SIGKILL, SIGHUP, SIGQUIT).
std::string_view signal_name(int signum);

/// The name of NTSTATUS `status` ("STATUS_DLL_NOT_FOUND" for 0xC0000135),
/// among those a Windows program ends with when it crashes or cannot start;
/// empty for another. On any host, for a Windows exit code read elsewhere.
std::string_view ntstatus_name(std::uint32_t status);

}  // namespace kota::sys
