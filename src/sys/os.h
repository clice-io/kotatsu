#pragma once

#include <cerrno>
#include <expected>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "kota/fs/windows.h"

// Declared by the driver kit only; ntdll has it from Windows 2000 on.
extern "C" LONG NTAPI RtlGetVersion(OSVERSIONINFOW* version);
#else
#include <unistd.h>
#endif

namespace kota::sys {

#ifdef _WIN32
/// Process `pid`, this one for 0, opened for `access`; closed when it goes.
struct ProcessHandle {
    HANDLE handle;

    explicit ProcessHandle(HANDLE handle) noexcept : handle(handle) {}

    ProcessHandle(ProcessHandle&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}

    ~ProcessHandle() {
        // The pseudo-handle of this process needs no closing.
        if(handle != nullptr && handle != ::GetCurrentProcess()) {
            ::CloseHandle(handle);
        }
    }

    static std::expected<ProcessHandle, std::error_code> open(int pid, DWORD access) {
        if(pid == 0) {
            return ProcessHandle(::GetCurrentProcess());
        }
        HANDLE handle = ::OpenProcess(access, FALSE, static_cast<DWORD>(pid));
        if(handle == nullptr) {
            // What OpenProcess answers for a pid no process has.
            if(::GetLastError() == ERROR_INVALID_PARAMETER) {
                return std::unexpected(std::make_error_code(std::errc::no_such_process));
            }
            return std::unexpected(fs::windows::last_error());
        }
        return ProcessHandle(handle);
    }
};
#else
/// The error errno holds.
inline std::error_code errno_code() {
    return {errno, std::generic_category()};
}
#endif

}  // namespace kota::sys
