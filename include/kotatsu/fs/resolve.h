#pragma once

#include <string_view>
#include <system_error>

#include "kotatsu/support/buffer_ref.h"
#include "kotatsu/fs/path.h"

// The functions here write a path into a buffer the caller gives, as
// path::push() does; the path they take may view that buffer.

namespace kotatsu::fs {

/// Writes into `out` the name the OS gives the existing file `path`
/// names: absolute, every symlink followed; on Windows also with each name's
/// on-disk case and long form, and through junctions and substituted
/// drives; on a volume with neither a drive letter nor a mount folder,
/// under the volume's GUID (`\\?\Volume{...}\`). Asks for no access, so a
/// file the caller may not read resolves too.
std::error_code canonical(buffer_ref out, std::string_view path);

/// Writes into `out` canonical() of the longest part of `path` that exists,
/// with the rest appended and normalized, `..` folded or kept as `dot_dot`
/// says: one name for every spelling of a file, whether it exists yet or
/// not. `path` is made absolute first, on Windows by Win32's own rules; a
/// path no part of which resolves is written normalized. Safe to call from
/// any thread: it reads the current directory only to make a relative path
/// absolute.
///
/// A `..` folded in the part that does not exist can name an existing file
/// where the OS would fail: `missing/../a` gives `a`. Kept, it stays, so
/// that the name fails where the OS fails it. On Windows, where Win32 folds
/// `..` by text before it resolves anything, it stays only in a verbatim
/// path (`\\?\`). It never fails: a part the OS cannot resolve, for any
/// reason, is taken as not existing.
void weakly_canonical(buffer_ref out,
                      std::string_view path,
                      path::DotDot dot_dot = path::DotDot::Fold);

/// Writes into `out` where the symlinks at the end of `path` lead: while the
/// last name is a symlink, its target, which, where relative, lies in the
/// link's directory and is pushed onto it, `..` and all, since the directory
/// could be a symlink too; on Windows, which resolves a link's `..` by text,
/// normalized. `path` itself where it is no symlink; the last target need
/// not exist. Symlinks before the last name stay as they are. More than 40
/// links in a row, which a loop of them makes, fail with
/// std::errc::too_many_symbolic_link_levels; a link whose target the system
/// cannot read, with the error read_symlink() gives; a path with a null in
/// it, with invalid_argument.
std::error_code follow_symlinks(buffer_ref out, std::string_view path);

/// Writes into `out` `path` made absolute against the current directory,
/// symlinks left alone. On Windows by Win32's own rules, which fold `.` and
/// `..`, take a rooted `\x` and a drive-relative `C:x` on their drive, and
/// remove trailing dots and spaces, but leave the names of a verbatim path
/// (`\\?\`) as they are, the prefix dropped as from_native() drops it; on
/// POSIX joined to the current directory with `.` and doubled separators
/// folded, but `..` kept, which a symlink before it would make name another
/// directory. Fails with std::errc::invalid_argument on an empty path.
std::error_code absolute(buffer_ref out, std::string_view path);

/// Writes into `out` the current directory, as the OS reports it.
std::error_code current_dir(buffer_ref out);

}  // namespace kotatsu::fs
