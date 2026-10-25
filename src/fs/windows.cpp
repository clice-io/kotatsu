#ifdef _WIN32

#include "kota/fs/windows.h"

#include <string>
#include <string_view>

#include "win32.h"
#include "kota/support/utf16.h"

namespace kota::fs::windows {

namespace {

/// The longest path the file functions take without `\\?\`: MAX_PATH less
/// the 12 units CreateDirectoryW keeps for an 8.3 name.
constexpr std::size_t short_path_limit = 248;

/// Whether `path` is absolute in the spelling Win32 resolves it to: `C:\x`
/// or a share, `\` only, with no empty (but a trailing one), `.` or `..`
/// component, and no name ending in a dot or space, which Win32 would remove.
bool needs_no_resolution(std::wstring_view path) {
    if(path.find(L'/') != std::wstring_view::npos) {
        return false;
    }
    bool drive = path.size() >= 3 && path[1] == L':' && path[2] == L'\\' &&
                 ((path[0] >= L'a' && path[0] <= L'z') || (path[0] >= L'A' && path[0] <= L'Z'));
    bool share = path.starts_with(LR"(\\)");
    if(!drive && !share) {
        return false;
    }
    std::size_t start = drive ? 3 : 2;
    while(start < path.size()) {
        auto end = path.find(L'\\', start);
        if(end == std::wstring_view::npos) {
            end = path.size();
        }
        auto name = path.substr(start, end - start);
        if(name.empty() || name == L"." || name == L".." || name.back() == L'.' ||
           name.back() == L' ') {
            return false;
        }
        start = end + 1;
    }
    return true;
}

}  // namespace

std::error_code to_error_code(unsigned long code) {
    auto errc = [&]() -> std::errc {
        switch(code) {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
            case ERROR_INVALID_DRIVE:
            case ERROR_BAD_NETPATH:
            case ERROR_BAD_NET_NAME:
            case ERROR_INVALID_NAME:
            case ERROR_BAD_PATHNAME: return std::errc::no_such_file_or_directory;
            case ERROR_DIRECTORY: return std::errc::not_a_directory;
            case ERROR_DIRECTORY_NOT_SUPPORTED: return std::errc::is_a_directory;
            case ERROR_ACCESS_DENIED:
            case ERROR_CANT_ACCESS_FILE:
            case ERROR_CANNOT_MAKE:
            case ERROR_CURRENT_DIRECTORY:
            case ERROR_WRITE_PROTECT: return std::errc::permission_denied;
            case ERROR_PRIVILEGE_NOT_HELD: return std::errc::operation_not_permitted;
            case ERROR_SHARING_VIOLATION:
            case ERROR_LOCK_VIOLATION:
            case ERROR_BUSY: return std::errc::device_or_resource_busy;
            case ERROR_ALREADY_EXISTS:
            case ERROR_FILE_EXISTS: return std::errc::file_exists;
            case ERROR_DIR_NOT_EMPTY: return std::errc::directory_not_empty;
            case ERROR_NOT_SAME_DEVICE: return std::errc::cross_device_link;
            case ERROR_FILENAME_EXCED_RANGE:
            case ERROR_BUFFER_OVERFLOW: return std::errc::filename_too_long;
            case ERROR_CANT_RESOLVE_FILENAME: return std::errc::too_many_symbolic_link_levels;
            case ERROR_NOT_ENOUGH_MEMORY:
            case ERROR_OUTOFMEMORY: return std::errc::not_enough_memory;
            case ERROR_DISK_FULL:
            case ERROR_HANDLE_DISK_FULL: return std::errc::no_space_on_device;
            case ERROR_TOO_MANY_OPEN_FILES: return std::errc::too_many_files_open;
            case ERROR_INVALID_HANDLE: return std::errc::bad_file_descriptor;
            case ERROR_INVALID_PARAMETER: return std::errc::invalid_argument;
            case ERROR_INVALID_FUNCTION: return std::errc::function_not_supported;
            case ERROR_NOT_SUPPORTED: return std::errc::not_supported;
            case ERROR_BROKEN_PIPE:
            case ERROR_NO_DATA: return std::errc::broken_pipe;
            case ERROR_NEGATIVE_SEEK: return std::errc::invalid_seek;
            case ERROR_SEEK:
            case ERROR_CRC:
            case ERROR_IO_DEVICE: return std::errc::io_error;
            case ERROR_NOT_READY:
            case ERROR_DEV_NOT_EXIST: return std::errc::no_such_device;
            case ERROR_OPERATION_ABORTED: return std::errc::operation_canceled;
            case ERROR_TIMEOUT: return std::errc::timed_out;
            case ERROR_BAD_EXE_FORMAT: return std::errc::executable_format_error;
            case ERROR_NO_UNICODE_TRANSLATION: return std::errc::illegal_byte_sequence;
            default: return {};
        }
    }();
    if(errc == std::errc{}) {
        return std::error_code(static_cast<int>(code), std::system_category());
    }
    return std::make_error_code(errc);
}

std::error_code last_error() {
    return to_error_code(::GetLastError());
}

std::expected<std::wstring, std::error_code> native_path(std::string_view path) {
    auto wide = win32::widen(path);
    if(!wide) {
        return std::unexpected(wide.error());
    }
    if(wide->starts_with(LR"(\\?\)") ||
       (wide->size() < short_path_limit && needs_no_resolution(*wide))) {
        return std::move(*wide);
    }
    auto full = win32::full_path(*wide);
    if(!full) {
        return std::unexpected(full.error());
    }
    return win32::verbatim(std::move(*full));
}

std::string from_native(std::wstring_view path) {
    std::wstring plain;
    if(path.starts_with(LR"(\\?\UNC\)")) {
        plain = LR"(\\)" + std::wstring(path.substr(8));
    } else if(path.starts_with(LR"(\\?\)") && path.size() >= 6 && path[5] == L':') {
        plain = std::wstring(path.substr(4));
    } else {
        return from_utf16(path);
    }
    // Without the prefix Win32 resolves the path: it names the same file
    // only where that resolution changes nothing.
    auto resolved = win32::full_path(plain);
    if(resolved && *resolved == plain) {
        return from_utf16(plain);
    }
    return from_utf16(path);
}

}  // namespace kota::fs::windows

namespace kota::fs::win32 {

std::expected<std::wstring, std::error_code> widen(std::string_view path) {
    if(path.find('\0') != std::string_view::npos) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    return to_utf16<wchar_t>(path);
}

std::expected<std::wstring, std::error_code> full_path(const std::wstring& path) {
    return fill_wide([&](wchar_t* buffer, DWORD size) {
        return ::GetFullPathNameW(path.c_str(), size, buffer, nullptr);
    });
}

std::wstring verbatim(std::wstring resolved) {
    if(resolved.starts_with(LR"(\\?\)") || resolved.starts_with(LR"(\\.\)")) {
        return resolved;
    }
    if(resolved.starts_with(LR"(\\)")) {
        return LR"(\\?\UNC\)" + resolved.substr(2);
    }
    return LR"(\\?\)" + resolved;
}

Handle try_open(const std::wstring& path, DWORD access, DWORD flags) {
    return Handle(::CreateFileW(path.c_str(),
                                access,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | flags,
                                nullptr));
}

std::expected<Handle, std::error_code> open(std::string_view path, DWORD access) {
    auto native = windows::native_path(path);
    if(!native) {
        return std::unexpected(native.error());
    }
    auto handle = try_open(*native, access, 0);
    if(handle.get() == INVALID_HANDLE_VALUE) {
        return std::unexpected(windows::last_error());
    }
    return handle;
}

std::expected<FileID, std::error_code> file_id(HANDLE handle) {
    FILE_ID_INFO id;
    if(::GetFileInformationByHandleEx(handle, FileIdInfo, &id, sizeof(id))) {
        return id_of(id.VolumeSerialNumber, id.FileID);
    }
    BY_HANDLE_FILE_INFORMATION info;
    if(!::GetFileInformationByHandle(handle, &info)) {
        return std::unexpected(windows::last_error());
    }
    return FileID{.device = info.dwVolumeSerialNumber,
                  .file = (std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow};
}

}  // namespace kota::fs::win32

#endif
