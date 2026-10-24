#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <system_error>

namespace kota::fs {

/// The name the OS gives the existing file `path` names: absolute, every
/// symlink followed; on Windows also with each name's on-disk case and long
/// form, and through junctions and substituted drives. Asks for no access,
/// so a file the caller may not read resolves too.
std::expected<std::string, std::error_code> canonical(std::string_view path);

/// canonical() of the longest part of `path` that exists, with the rest
/// appended and normalized, `..` folded: one name for every spelling of a
/// file, whether it exists yet or not. `path` is made absolute first, on
/// Windows by Win32's own rules; a path no part of which resolves comes
/// back normalized. Safe to call from any thread: it reads the current
/// directory only to make a relative path absolute.
///
/// The `..` in the part that does not exist folds by text, which can name
/// an existing file where the OS would fail: `missing/../a` gives `a`.
std::string weakly_canonical(std::string_view path);

/// `path` made absolute against the current directory, symlinks left
/// alone. On Windows by Win32's own rules, which fold `.` and `..`, take a
/// rooted `\x` and a drive-relative `C:x` on their drive, and remove
/// trailing dots and spaces; on POSIX joined to the current directory with
/// `.` and doubled separators folded, but `..` kept, which a symlink before
/// it would make name another directory. Fails with
/// std::errc::invalid_argument on an empty path.
std::expected<std::string, std::error_code> absolute(std::string_view path);

/// The current directory, as the OS reports it.
std::expected<std::string, std::error_code> current_dir();

}  // namespace kota::fs
