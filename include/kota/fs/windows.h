#pragma once

#ifdef _WIN32

#include <string_view>
#include <system_error>

#include "kota/support/buffer_ref.h"

/// Where kotatsu's UTF-8 paths meet the Windows API: the wide paths its file
/// functions take, and its errors as std::error_code.
namespace kota::fs::windows {

/// A Win32 error code, as GetLastError() reports it: a std::errc code where
/// the error has a POSIX counterpart, so that `ec ==
/// std::errc::no_such_file_or_directory` holds with any standard library;
/// otherwise the code itself, in std::system_category().
std::error_code to_error_code(unsigned long code);

/// to_error_code(GetLastError()).
std::error_code last_error();

/// Writes into `out` `path` as the Windows file functions take it, but for
/// the NUL that ends it: UTF-16, and prefixed `\\?\` (`\\?\UNC\` for a
/// share, in place of `\\.\` for a device path past MAX_PATH) so that its
/// length is no limit. A path the prefix would change goes
/// through Win32's own resolution first, the resolution the plain spelling
/// gets: a relative, rooted (`\x`) or drive-relative (`C:x`) path is taken
/// against the current directory, `.` and `..` fold, trailing dots and spaces
/// go. A short absolute path needs no prefix and comes out as it is, as does
/// a verbatim one. Fails with std::errc::invalid_argument on a NUL, and with
/// std::errc::illegal_byte_sequence on text that is not WTF-8.
std::error_code to_native(basic_buffer_ref<wchar_t> out, std::string_view path);

/// Writes into `out` a path Windows gave, as WTF-8, without the `\\?\` or
/// `\\?\UNC\` prefix wherever the plain spelling names the same file.
void from_native(buffer_ref out, std::wstring_view path);

}  // namespace kota::fs::windows

#endif
