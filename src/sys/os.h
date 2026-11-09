#pragma once

#include <cerrno>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <cstdint>
#include <cstdlib>
#include <io.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "kotatsu/support/buffer_ref.h"
#include "kotatsu/support/small_string.h"
#include "kotatsu/fs/windows.h"

// Declared by the driver kit only; ntdll has it from Windows 2000 on.
extern "C" LONG NTAPI RtlGetVersion(OSVERSIONINFOW* version);
#else
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mach-o/loader.h>
#endif

namespace kotatsu::sys {

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

#ifdef _MSC_VER
/// Does nothing, for _get_osfhandle: the C runtime would end the process
/// for a descriptor not open.
inline void ignore_invalid_parameter(const wchar_t*,
                                     const wchar_t*,
                                     const wchar_t*,
                                     unsigned int,
                                     std::uintptr_t) {}
#endif

/// The handle of C runtime descriptor `fd`, as _get_osfhandle gives it but
/// ending no process: INVALID_HANDLE_VALUE for a descriptor not open, and -2
/// for a standard stream without a handle, the pseudo-handles of this process
/// and of this thread, neither a console, a pipe nor a file.
inline HANDLE descriptor_handle(int fd) noexcept {
#ifdef _MSC_VER
    auto previous = ::_set_thread_local_invalid_parameter_handler(ignore_invalid_parameter);
    auto handle = ::_get_osfhandle(fd);
    ::_set_thread_local_invalid_parameter_handler(previous);
#else
    // MinGW's headers declare no handler of a thread's own; its startup code
    // sets one for the process that does nothing, so the runtime says EBADF
    // and nothing more.
    auto handle = ::_get_osfhandle(fd);
#endif
    return reinterpret_cast<HANDLE>(handle);
}

/// Writes the start of non-empty `text` to `handle` as write_console_some()
/// does to a descriptor.
std::expected<std::size_t, std::error_code> write_handle_some(HANDLE handle,
                                                              std::string_view text) noexcept;

/// The path module `module` was loaded by, as GetModuleFileNameW gives it,
/// however long, in `path`.
std::wstring_view module_file_name(HMODULE module, small_wstring<264>& path);

/// Writes into `out` the system directory, as GetSystemDirectoryW gives it.
std::error_code system_directory(buffer_ref out);

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

#ifdef __APPLE__
/// The first load command of type `type` of the image whose header is at
/// `header` that `match` holds for: null for none.
template <typename Command, typename Match>
const Command* find_command(const mach_header* header, std::uint32_t type, Match match) noexcept {
    const auto* command =
        reinterpret_cast<const load_command*>(reinterpret_cast<const mach_header_64*>(header) + 1);
    for(std::uint32_t index = 0; index < header->ncmds; ++index) {
        const auto* found = reinterpret_cast<const Command*>(command);
        if(command->cmd == type && match(*found)) {
            return found;
        }
        command = reinterpret_cast<const load_command*>(reinterpret_cast<const char*>(command) +
                                                        command->cmdsize);
    }
    return nullptr;
}

/// The first load command of type `type`: null for none.
template <typename Command>
const Command* find_command(const mach_header* header, std::uint32_t type) noexcept {
    return find_command<Command>(header, type, [](const Command&) { return true; });
}

/// The segment named `name` of the image whose header is at `header`, one
/// every image has, as __TEXT and __LINKEDIT.
const inline segment_command_64& find_segment(const mach_header* header,
                                              std::string_view name) noexcept {
    const auto* segment = find_command<segment_command_64>(
        header,
        LC_SEGMENT_64,
        [&](const segment_command_64& segment) {
            return std::string_view(segment.segname,
                                    ::strnlen(segment.segname, sizeof(segment.segname))) == name;
        });
    assert(segment != nullptr);
    return *segment;
}
#endif

}  // namespace kotatsu::sys
