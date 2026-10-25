#pragma once

// <windows.h> for the tests that call Win32 themselves, without the min and
// max macros that break the standard library's, and make_pipe() for those
// that need a named pipe.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <string>
#include <string_view>
#include <windows.h>

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/support/utf16.h"
#include "kota/fs/path.h"

namespace kota::test {

/// A named pipe of `access`, its name kept apart by the directory's, and
/// written into `name` in UTF-8.
inline HANDLE make_pipe(std::string& name, const TempDir& dir, DWORD access, DWORD buffer) {
    auto wide = LR"(\\.\pipe\)" + std_path(fs::path::filename(dir.root())).wstring();
    HANDLE pipe = ::CreateNamedPipeW(wide.c_str(),
                                     access,
                                     PIPE_TYPE_BYTE | PIPE_WAIT,
                                     1,
                                     buffer,
                                     buffer,
                                     0,
                                     nullptr);
    ZASSERT(pipe != INVALID_HANDLE_VALUE);
    from_utf16(name, std::wstring_view(wide));
    return pipe;
}

}  // namespace kota::test
#endif
