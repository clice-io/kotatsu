#pragma once

// <windows.h> for the tests that call Win32 themselves, without the min and
// max macros that break the standard library's, attributes_of() for those
// that check a file's attributes, make_pipe() for those that need a named
// pipe, set_reparse_point() for those that need a reparse point of a tag no
// tool makes and DeletePending for those that need a file deleted while held
// open.

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
#include <winioctl.h>

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/support/utf16.h"
#include "kota/fs/path.h"

namespace kota::test {

/// The attributes of the file at the UTF-8 `path`.
inline DWORD attributes_of(std::string_view path) {
    auto attributes = ::GetFileAttributesW(std_path(path).wstring().c_str());
    ZASSERT(attributes != INVALID_FILE_ATTRIBUTES);
    return attributes;
}

/// A named pipe of `access`, its name kept apart by the directory's, and
/// written into `name` in UTF-8; another of the same directory is another
/// instance of it.
inline HANDLE make_pipe(std::string& name, const TempDir& dir, DWORD access, DWORD buffer) {
    auto wide = LR"(\\.\pipe\)" + std_path(fs::path::filename(dir.root())).wstring();
    HANDLE pipe = ::CreateNamedPipeW(wide.c_str(),
                                     access,
                                     PIPE_TYPE_BYTE | PIPE_WAIT,
                                     PIPE_UNLIMITED_INSTANCES,
                                     buffer,
                                     buffer,
                                     0,
                                     nullptr);
    ZASSERT(pipe != INVALID_HANDLE_VALUE);
    from_utf16(name, std::wstring_view(wide));
    return pipe;
}

/// Makes the file at `path` a reparse point of the Microsoft `tag` with no
/// data, as WSL makes its fifos and devices.
inline void set_reparse_point(std::string_view path, DWORD tag) {
    HANDLE file = ::CreateFileW(std_path(path).wstring().c_str(),
                                GENERIC_WRITE,
                                0,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT,
                                nullptr);
    ZASSERT(file != INVALID_HANDLE_VALUE);

    // REPARSE_DATA_BUFFER's header, which only the driver headers declare.
    struct {
        DWORD tag;
        WORD length = 0;
        WORD reserved = 0;
    } header{.tag = tag};

    DWORD returned = 0;
    bool set = ::DeviceIoControl(file,
                                 FSCTL_SET_REPARSE_POINT,
                                 &header,
                                 sizeof(header),
                                 nullptr,
                                 0,
                                 &returned,
                                 nullptr);
    ::CloseHandle(file);
    ZASSERT(set);
}

/// The file at the UTF-8 `path` deleted the classic way, which keeps its
/// name until the last handle to it goes and lets no open through it
/// meanwhile, while this holds a handle to it.
struct DeletePending {
    explicit DeletePending(std::string_view path) :
        handle(::CreateFileW(std_path(path).wstring().c_str(),
                             DELETE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr,
                             OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS,
                             nullptr)) {
        ZASSERT(handle != INVALID_HANDLE_VALUE);
        FILE_DISPOSITION_INFO info{TRUE};
        ZASSERT(::SetFileInformationByHandle(handle, FileDispositionInfo, &info, sizeof(info)));
    }

    DeletePending(const DeletePending&) = delete;
    DeletePending& operator=(const DeletePending&) = delete;

    ~DeletePending() {
        ::CloseHandle(handle);
    }

    HANDLE handle;
};

}  // namespace kota::test
#endif
