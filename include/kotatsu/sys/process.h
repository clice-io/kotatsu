#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "kotatsu/support/buffer_ref.h"

namespace kotatsu::sys {

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

/// Where this program's executable is loaded, what the addresses of its
/// code are offset by from those of its file: on Linux its ELF load bias, as
/// the dynamic loader takes it from where the program headers are and where
/// PT_PHDR puts them (0 without one, a static executable not made to be
/// moved); on macOS its Mach-O slide; on Windows its module handle, the
/// address of its PE image. What write_stacktrace() names the executable's
/// frames by. Async-signal-safe: no call to the loader.
std::uintptr_t executable_base() noexcept;

/// Writes into `out` the path this program was started by, links not
/// resolved: started through a symbolic or a hard link to its file, as a
/// program that acts as whichever it is named after is, it names the link.
/// Unlike argv[0], which the starter picks, it is a path that reached the
/// file; unlike executable_path(), it does not follow a rename. `..` stays
/// in it. On Linux what exec was given (AT_EXECFN), relative to the
/// directory the program started in when that was relative; on macOS that
/// made absolute against that directory (_NSGetExecutablePath); on Windows
/// the absolute path CreateProcess took (GetModuleFileNameW).
void started_path(buffer_ref out);

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
/// C runtime's own argv on macOS, and on Windows GetCommandLineW() split as
/// cmd::Quoting::Windows says, in WTF-8, without the wildcard expansion
/// some C runtimes add. Read once; a title set later leaves them as they
/// were. Empty on Linux when /proc cannot be read.
const std::vector<std::string>& arguments();

/// Sets the title `ps` shows for this process: the memory of its arguments
/// is overwritten with `title`, cut to their length, and on Linux the name
/// `top` shows, as set_process_name sets it, unless nothing of `title` is
/// left for it, as of an empty one.
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
/// only files that cannot be run are, `out` then naming the first of them:
/// for Cygwin symlinks, the link that names a POSIX path or nothing that
/// runs, whose target fs::cygwin::read_symlink reads, or the first of links
/// that loop.
std::error_code find_program(buffer_ref out,
                             std::string_view name,
                             const ProgramSearch& search = {});

/// Ends this process once its parent exits, as SIGKILL would: what
/// CreationOptions::exit_with_parent asks of a child, asked by the child
/// itself, as it has to be on macOS. On Linux by PR_SET_PDEATHSIG, which the
/// kernel keeps for the calling thread: call it on one that lasts as long as
/// the process, the main thread. It watches the thread of the parent that
/// started this process, and so ends this one too when that thread exits
/// first, and a change of credentials clears it, an exec of a set-user-ID
/// program among them. On macOS a thread of its own waits for the parent's
/// exit (kqueue's NOTE_EXIT). On Windows a wait of the thread pool on the
/// parent's handle ends it, with exit code 1. A parent that has exited by
/// the call ends this process there on Windows, where a process that took
/// its ID since is told apart by its later start; on POSIX the parent is by
/// then the one that adopted this process, init or a subreaper, whose exit
/// it waits for instead. Fails when the parent cannot be watched: on Windows
/// with permission_denied for one this process may not open.
std::error_code exit_with_parent();

/// Ignores SIGPIPE when its action is the default, as an event loop does: a
/// write to a pipe or a socket whose reader went away then fails with EPIPE
/// rather than ending the process. A child started with keep_signals gets
/// the default back, which it would not after a signal() of the caller's
/// own. Nothing on Windows, which has no SIGPIPE.
void ignore_sigpipe() noexcept;

/// The numbers of SIGHUP and SIGKILL, which Windows' <csignal> does not
/// name, on every host: POSIX's. On Windows async::signal takes sighup for
/// the console closing, and a Child's kill takes sigkill to end it.
constexpr inline int sighup = 1;
constexpr inline int sigkill = 9;

/// The name of signal `signum` on this host ("SIGSEGV"); empty for a number
/// it has no signal by. On Windows the signals of the C runtime, and SIGHUP
/// and SIGKILL by POSIX's numbers, sighup and sigkill.
std::string_view signal_name(int signum);

}  // namespace kotatsu::sys
