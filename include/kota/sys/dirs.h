#pragma once

#include <expected>
#include <string>
#include <system_error>

// The user's well-known directories, none of which is created. On Windows,
// linking home_dir, cache_dir, config_dir or data_dir imports shell32 and
// ole32 for the known folders, which a library injected into other programs
// should not load; temp_dir lives apart and takes kernel32 alone.

namespace kota::sys {

/// The user's home directory: HOME, on Windows USERPROFILE, when it is set
/// and not empty; else the user's entry in the password database, on Windows
/// the profile directory.
std::expected<std::string, std::error_code> home_dir();

/// The directory for temporary files: the first of TMPDIR, TMP, TEMP and
/// TEMPDIR set and not empty, else /tmp; on Windows GetTempPath2W, which
/// gives a SYSTEM process a directory of its own, or GetTempPathW where it is
/// missing. Normalized, `..` kept: no trailing separator but at a root.
std::expected<std::string, std::error_code> temp_dir();

/// The user's directory for caches: XDG_CACHE_HOME when it is absolute, else
/// ~/.cache; ~/Library/Caches on macOS; the local application data on
/// Windows.
std::expected<std::string, std::error_code> cache_dir();

/// The user's directory for settings: XDG_CONFIG_HOME when it is absolute,
/// else ~/.config; ~/Library/Application Support on macOS; the roaming
/// application data on Windows.
std::expected<std::string, std::error_code> config_dir();

/// The user's directory for data: XDG_DATA_HOME when it is absolute, else
/// ~/.local/share; ~/Library/Application Support on macOS; the local
/// application data on Windows, which does not follow the user to other
/// machines.
std::expected<std::string, std::error_code> data_dir();

}  // namespace kota::sys
