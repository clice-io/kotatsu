#pragma once

#include <cerrno>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "kota/support/utf16.h"
#include "kota/fs/windows.h"

// Declared by the driver kit only; ntdll has it from Windows 2000 on.
extern "C" LONG NTAPI RtlGetVersion(OSVERSIONINFOW* version);
#else
#include <unistd.h>
#endif

namespace kota::sys {

#ifdef _WIN32
/// A handle, closed when this goes.
struct Handle {
    HANDLE handle = nullptr;

    Handle() = default;

    explicit Handle(HANDLE handle) noexcept : handle(handle) {}

    Handle(Handle&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}

    Handle& operator=(Handle&& other) noexcept {
        std::swap(handle, other.handle);
        return *this;
    }

    ~Handle() {
        // This process's pseudo-handle needs no closing, nor gets any.
        if(handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle);
        }
    }

    HANDLE release() noexcept {
        return std::exchange(handle, nullptr);
    }

    /// Process `pid`, this one for 0, opened for `access`.
    static std::expected<Handle, std::error_code> open_process(int pid, DWORD access) {
        if(pid == 0) {
            return Handle(::GetCurrentProcess());
        }
        HANDLE handle = ::OpenProcess(access, FALSE, static_cast<DWORD>(pid));
        if(handle == nullptr) {
            // What OpenProcess answers for a pid no process has.
            if(::GetLastError() == ERROR_INVALID_PARAMETER) {
                return std::unexpected(std::make_error_code(std::errc::no_such_process));
            }
            return std::unexpected(fs::windows::last_error());
        }
        return Handle(handle);
    }
};

/// `text` in UTF-16, for the Windows functions.
inline std::expected<std::wstring, std::error_code> wide(std::string_view text) {
    std::wstring result;
    if(auto error = to_utf16(result, text)) {
        return std::unexpected(error);
    }
    return result;
}

/// `text`, from the Windows functions, as WTF-8.
inline std::string narrow(std::wstring_view text) {
    std::string result;
    from_utf16(result, text);
    return result;
}

/// The directories CreateProcess searches for a program after this
/// program's and the current one, before PATH: the system directory, the
/// 16-bit system directory and the Windows directory.
std::expected<std::vector<std::string>, std::error_code> system_directories();
#else
/// The error errno holds.
inline std::error_code errno_code() {
    return {errno, std::generic_category()};
}

/// The directories execvp searches without PATH: confstr(_CS_PATH)'s.
inline std::string default_path() {
    std::string path(::confstr(_CS_PATH, nullptr, 0), '\0');
    ::confstr(_CS_PATH, path.data(), path.size());
    path.pop_back();
    return path;
}
#endif

}  // namespace kota::sys
