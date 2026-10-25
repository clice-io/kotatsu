#pragma once

#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "kota/support/buffer_ref.h"

namespace kota::sys {

/// This process's ID.
int pid() noexcept;

/// The ID of this process's parent. On POSIX the parent it has now: init or
/// a subreaper once the one that started it has exited. On Windows the
/// process that started it, which may have exited and its ID gone to another.
int parent_pid() noexcept;

/// Whether process `pid` exists, one this process may not signal or open
/// included: on POSIX a zombie too, which has exited and which its parent
/// has not waited for. False for 0 and below. An ID may be another
/// process's once its own has gone.
bool process_alive(int pid) noexcept;

/// Writes into `out` the absolute path of this program's executable, under
/// the name its file has now: renamed since the program started, the new
/// name; symbolic links resolved. Fails with no_such_file_or_directory once
/// the file was removed, as an update that replaced it removes it; Windows
/// removes no file a program runs from.
std::error_code executable_path(buffer_ref out);

/// Writes into `out` a path that starts this very program, the image this
/// process runs, for this process to start another copy of it that speaks
/// its protocol: on Linux /proc/self/exe, which reaches the image after its
/// file was renamed, replaced or removed, but names another's in another
/// process, and makes a poor argv[0] (SpawnOptions::arg0 sets one);
/// elsewhere executable_path(), which fails on macOS once the file was
/// removed.
std::error_code self_executable(buffer_ref out);

/// The arguments this program was started with, argv[0] first, read from
/// the system rather than from main's argv: /proc/self/cmdline on Linux, the
/// C runtime's own argv on macOS, and on Windows GetCommandLineW() split by
/// windows::split_command_line, in WTF-8, without the wildcard expansion
/// some C runtimes add. Read once; a title set later leaves them as they
/// were. Empty on Linux when /proc cannot be read.
const std::vector<std::string>& arguments();

/// Sets the title `ps` shows for this process: the memory of its arguments
/// is overwritten with `title`, cut to their length, and on Linux the name
/// `top` shows, as set_process_name sets it, unless `title` is empty.
/// not_supported on Windows, which has no such title (its console's is
/// shared with the processes before this one). Not thread-safe against
/// readers of main's argv.
std::error_code set_process_title(std::string_view title);

/// Sets the name `top` and `pgrep` show for this process, leaving its
/// arguments as they are: on Linux the name of its main thread, the first 15
/// bytes of `name`, cut back to a character's start. invalid_argument when
/// that leaves nothing, as of an empty `name`. not_supported on macOS and
/// Windows.
std::error_code set_process_name(std::string_view name);

/// Where find_program looks for a program.
struct ProgramSearch {
    /// The directories, in order: PATH's entries when empty, and on POSIX
    /// confstr(_CS_PATH)'s ("/bin:/usr/bin") when PATH is unset. An empty
    /// entry is the current directory on POSIX and nothing on Windows, whose
    /// current directory is searched only when listed, as cmd lists it first.
    std::vector<std::string> directories = {};

    /// Windows: the extensions a name is tried with, compared ignoring case;
    /// when empty, PATHEXT's (.COM;.EXE;.BAT;.CMD when it is unset or lists
    /// none). A name that has one of them is tried as it is, another with
    /// each of them in turn, in one directory before the next; "" stands for
    /// the name as it is. POSIX programs have no extensions: ignored there.
    std::vector<std::string> extensions = {};
};

/// Writes into `out` the file program `name` names. On POSIX as execvp
/// finds it: `name` itself when it holds a `/`, else the first regular file
/// with execute permission in the directories of `search`. On Windows the
/// first file that is no directory, a name with a separator or a drive being
/// taken as a path rather than looked for; a Cygwin symlink (`/usr/bin/cc`)
/// is followed to the program it names beside it, `.exe` added when the
/// target has none, and counts as a file that cannot be run when it names a
/// POSIX path, which only its runtime's mount table maps.
/// no_such_file_or_directory when nothing is found, permission_denied when
/// only files that cannot be run are.
std::error_code find_program(buffer_ref out,
                             std::string_view name,
                             const ProgramSearch& search = {});

/// The name of signal `signum` on this host ("SIGSEGV"); empty for a number
/// it has no signal by. On Windows the signals of the C runtime, and SIGKILL
/// by POSIX's number, 9, which a child's kill takes to end it.
std::string_view signal_name(int signum);

}  // namespace kota::sys
