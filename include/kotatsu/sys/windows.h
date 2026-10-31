#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "kotatsu/support/buffer_ref.h"
#include "kotatsu/support/string_list_ref.h"

// What only Windows has. The names of exit codes are declared on every host,
// for exit codes read elsewhere; what asks Windows itself only on Windows.

namespace kotatsu::sys::windows {

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

/// Writes into `out` the file CreateProcess runs for program `name` from
/// `directories`, by its rules: a name with a separator or a drive is a
/// path, looked for nowhere else; `.exe` is added only to a name without a
/// dot, which CreateProcess takes for an extension of its own, a leading one
/// (`.bat`) too; and a Cygwin symlink is the file it is, which
/// CreateProcess cannot start. Errors, and what `out` names on
/// permission_denied, as find_program's.
std::error_code find_create_process_program(buffer_ref out,
                                            std::string_view name,
                                            string_list_ref directories);

#endif

}  // namespace kotatsu::sys::windows
