#pragma once

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "kota/fs/path.h"

// The environment of this process. On Windows it is the process's own block
// (GetEnvironmentVariableW and the like), never the copy the CRT's getenv and
// _putenv keep, which a change through one does not reach. On POSIX the
// functions here hold a lock between them, which code calling getenv and
// setenv directly does not take.

namespace kota::sys {

/// The value of variable `name`; unset when there is none, or `name` is no
/// variable's name.
std::optional<std::string> get_env(std::string_view name);

/// Sets variable `name` to `value`. invalid_argument for a name that is
/// empty, holds a NUL or holds '=' (on Windows past its first character, as
/// the `=C:` variables do), and for a value holding a NUL.
std::error_code set_env(std::string_view name, std::string_view value);

/// Removes variable `name`; nothing when there is none. invalid_argument for
/// a name set_env refuses.
std::error_code unset_env(std::string_view name);

/// Every variable, as "NAME=value". On Windows the `=C:` variables, the
/// current directory of each drive, are among them.
std::vector<std::string> environment();

/// Whether two variable names are the same: as Windows compares them,
/// ignoring case, on Windows; byte for byte elsewhere.
bool env_name_equal(std::string_view left, std::string_view right);

/// The entries of a list of paths such as PATH: separated by ':' in the
/// POSIX style, by ';' in the Windows style, where a '"' opens or closes a
/// part in which ';' separates nothing and is itself left out. Empty entries
/// are kept: POSIX reads them as the current directory. An empty list has
/// one empty entry in the POSIX style and none in the Windows style.
std::vector<std::string> split_paths(std::string_view list,
                                     fs::path::Style style = fs::path::native_style);

/// The list split_paths reads back as `paths`: in the Windows style an entry
/// holding ';' is quoted, and so is a lone empty one. In the POSIX style no
/// list has no entries: none joins to "", which reads back as one empty
/// entry. invalid_argument for an entry holding ':' in the POSIX style, or
/// '"' in the Windows style, which no list can hold.
std::expected<std::string, std::error_code>
    join_paths(std::span<const std::string> paths, fs::path::Style style = fs::path::native_style);

}  // namespace kota::sys
