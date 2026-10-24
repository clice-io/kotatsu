#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kota/fs/path.h"

/// The POSIX paths Cygwin and MSYS2 programs print and take on Windows.
namespace kota::fs::cygwin {

/// Whether `path` is a POSIX path a Cygwin or MSYS2 program wrote
/// (`/usr/include`, `/cygdrive/c/proj`), which a Windows program cannot
/// open: rooted, but not absolute in `style`. Never in the POSIX style,
/// where every rooted path is absolute.
bool is_posix_path(std::string_view path, path::Style style = path::native_style);

/// The mount table of a Cygwin or MSYS2 runtime, which maps the POSIX paths
/// its programs use to Windows paths. Only the runtime's own table is
/// right: Cygwin mounts `/usr/lib` at `<root>/lib`, MSYS2 its drives at `/c`
/// rather than `/cygdrive/c`, and a user's `/etc/fstab` adds mounts.
struct MountTable {
    /// Each mount point with the Windows directory it names, the longest
    /// point first.
    std::vector<std::pair<std::string, std::string>> mounts;

    /// The table as the runtime's `mount` lists it, one `<windows> on
    /// <point> type <fs> (<flags>)` line per mount; none when it mounts
    /// nothing at the root, where every POSIX path would fall.
    static std::optional<MountTable> parse(std::string_view listing);

    /// The Windows path, with forward slashes, that the POSIX path `posix`
    /// names. `.` and `..` fold first, as the runtime folds them, so `..`
    /// out of a mount point leaves the mount.
    std::string to_windows(std::string_view posix) const;
};

/// The directories that hold a Cygwin or MSYS2 runtime (cygwin1.dll,
/// msys-2.0.dll) in the order Windows looks for `program`'s DLLs: its own
/// directory, then each of `search_path`, PATH's directories. A program
/// loads the first; the others are where a POSIX path it printed may
/// belong when PATH holds two runtimes.
std::vector<std::string> runtime_dirs(std::string_view program,
                                      std::span<const std::string> search_path);

/// The target of the Cygwin symlink at `path`, a file holding the cookie
/// `!<symlink>` and the target, which the package installer writes for a
/// package's links (`/usr/bin/cc`) and Windows cannot follow. None when
/// `path` is no such file.
std::optional<std::string> read_symlink(std::string_view path);

}  // namespace kota::fs::cygwin
