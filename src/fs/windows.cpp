#ifdef _WIN32

#include "kotatsu/fs/windows.h"

#include <algorithm>
#include <cstring>
#include <string_view>

#include "win32.h"
#include "kotatsu/support/utf16.h"

namespace kotatsu::fs::windows {

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
            case ERROR_BAD_PATHNAME:
            // A file deleted while held open, as win32::open_at() tells it:
            // gone but for its name.
            case ERROR_DELETE_PENDING: return std::errc::no_such_file_or_directory;
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
            case ERROR_NEGATIVE_SEEK: return std::errc::invalid_argument;
            case ERROR_SEEK:
            case ERROR_CRC:
            case ERROR_READ_FAULT:
            case ERROR_WRITE_FAULT:
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

std::error_code to_native(basic_buffer_ref<wchar_t> out, std::string_view path) {
    if(auto error = win32::widen(out, path)) {
        return error;
    }
    auto wide = out.view();
    if(wide.starts_with(LR"(\\?\)") ||
       (wide.size() < short_path_limit && needs_no_resolution(wide))) {
        return {};
    }
    win32::WidePath plain;
    plain.assign(wide);
    if(auto error = win32::full_path(out, plain.c_str())) {
        return error;
    }
    win32::verbatim(out);
    return {};
}

void from_native(buffer_ref out, std::wstring_view path) {
    win32::WidePath plain;
    if(path.starts_with(LR"(\\?\UNC\)")) {
        plain.append(2, L'\\');
        plain.append(path.substr(8));
    } else if(path.starts_with(LR"(\\?\)") && path.size() >= 6 && path[5] == L':') {
        plain.append(path.substr(4));
    } else {
        from_utf16(out, path);
        return;
    }
    // Without the prefix Win32 resolves the path: it names the same file
    // only where that resolution changes nothing.
    win32::WidePath resolved;
    if(!win32::full_path(resolved, plain.c_str()) && std::ranges::equal(resolved, plain)) {
        from_utf16(out, plain);
        return;
    }
    from_utf16(out, path);
}

}  // namespace kotatsu::fs::windows

namespace kotatsu::fs::win32 {

DWORD dos_error(NTSTATUS status) {
    using ToDosErrorFn = ULONG(NTAPI*)(NTSTATUS);
    static constinit std::atomic<ToDosErrorFn> to_dos_error = nullptr;
    return from_ntdll(to_dos_error, "RtlNtStatusToDosError")(status);
}

std::error_code widen(basic_buffer_ref<wchar_t> out, std::string_view path) {
    if(path.find('\0') != std::string_view::npos) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    return to_utf16(out, path);
}

std::error_code full_path(basic_buffer_ref<wchar_t> out, const wchar_t* path) {
    return fill_wide(out, [&](wchar_t* buffer, DWORD size) {
        return ::GetFullPathNameW(path, size, buffer, nullptr);
    });
}

void verbatim(basic_buffer_ref<wchar_t> path) {
    auto held = path.view();
    if(held.starts_with(LR"(\\?\)")) {
        return;
    }
    // A device path stays as Win32 takes it, CON and CONIN$ among them, but
    // for the length it limits: the verbatim prefix names the same NT path.
    if(held.starts_with(LR"(\\.\)")) {
        if(held.size() >= windows::short_path_limit) {
            path.chars()[2] = L'?';
        }
        return;
    }
    // A share's `\\server` becomes `\\?\UNC\server`.
    bool share = held.starts_with(LR"(\\)");
    std::wstring_view prefix = share ? LR"(\\?\UNC\)" : LR"(\\?\)";
    std::size_t kept_from = share ? 2 : 0;
    auto size = held.size();
    auto chars = path.resize(size - kept_from + prefix.size());
    std::memmove(chars.data() + prefix.size(),
                 chars.data() + kept_from,
                 (size - kept_from) * sizeof(wchar_t));
    std::ranges::copy(prefix, chars.begin());
}

std::expected<Handle, DWORD>
    open_at(HANDLE root, std::wstring_view name, DWORD access, bool follow) {
    using NtCreateFileFn = NTSTATUS(NTAPI*)(PHANDLE,
                                            ACCESS_MASK,
                                            POBJECT_ATTRIBUTES,
                                            PIO_STATUS_BLOCK,
                                            PLARGE_INTEGER,
                                            ULONG,
                                            ULONG,
                                            ULONG,
                                            ULONG,
                                            PVOID,
                                            ULONG);
    static constinit std::atomic<NtCreateFileFn> create = nullptr;
    // NtCreateFile's disposition, options and status, which only the DDK
    // names.
    constexpr ULONG open_existing = 1;
    constexpr ULONG synchronous_io = 0x20;
    constexpr ULONG backup_intent = 0x4000;
    constexpr ULONG open_reparse_point = 0x200000;
    constexpr auto delete_pending = static_cast<NTSTATUS>(0xC0000056);
    // A UNICODE_STRING counts its bytes in 16 bits.
    if(name.size() > std::numeric_limits<USHORT>::max() / sizeof(wchar_t)) {
        return std::unexpected(DWORD{ERROR_FILENAME_EXCED_RANGE});
    }
    UNICODE_STRING object_name;
    object_name.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    object_name.MaximumLength = object_name.Length;
    object_name.Buffer = const_cast<PWSTR>(name.data());
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = root;
    attributes.ObjectName = &object_name;
    attributes.Attributes = OBJ_CASE_INSENSITIVE;
    IO_STATUS_BLOCK status_block{};
    HANDLE handle = INVALID_HANDLE_VALUE;
    auto status = from_ntdll(create, "NtCreateFile")(
        &handle,
        access | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        &attributes,
        &status_block,
        nullptr,
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        open_existing,
        synchronous_io | backup_intent | (follow ? 0 : open_reparse_point),
        nullptr,
        0);
    if(status == delete_pending) {
        return std::unexpected(DWORD{ERROR_DELETE_PENDING});
    }
    if(status < 0) {
        return std::unexpected(dos_error(status));
    }
    return Handle(handle);
}

std::expected<Handle, DWORD> open_native(std::wstring_view path, DWORD access, bool follow) {
    // NT spells Win32's prefixes `\??\`, a share's `\??\UNC\`; a path
    // to_native() leaves without one, which Win32 resolves to itself, takes
    // one all the same.
    WidePath name;
    name.append(std::wstring_view(LR"(\??\)"));
    if(path.starts_with(LR"(\\?\)") || path.starts_with(LR"(\\.\)")) {
        name.append(path.substr(4));
    } else if(path.starts_with(LR"(\\)")) {
        name.append(std::wstring_view(LR"(UNC\)"));
        name.append(path.substr(2));
    } else {
        name.append(path);
    }
    return open_at(nullptr, name, access, follow);
}

std::expected<Handle, std::error_code> open(std::string_view path, DWORD access) {
    win32::WidePath native;
    if(auto error = windows::to_native(native, path)) {
        return std::unexpected(error);
    }
    return open_native(native, access, true).transform_error([](DWORD error) {
        return windows::to_error_code(error);
    });
}

std::error_code final_path(buffer_ref out, HANDLE handle) {
    WidePath final;
    auto ask = [&](DWORD volume) {
        return fill_wide(final, [&](wchar_t* buffer, DWORD size) {
            return ::GetFinalPathNameByHandleW(handle, buffer, size, FILE_NAME_NORMALIZED | volume);
        });
    };
    // A volume with neither a drive letter nor a mount folder has no DOS
    // name, only its GUID's: `\\?\Volume{...}\`.
    auto error = ask(VOLUME_NAME_DOS);
    if(error == std::errc::no_such_file_or_directory) {
        error = ask(VOLUME_NAME_GUID);
    }
    if(error) {
        return error;
    }
    windows::from_native(out, final);
    return {};
}

bool set_attributes(HANDLE handle, DWORD attributes) {
    // Zero leaves the times as they are, and the attributes too: a file with
    // none says so.
    FILE_BASIC_INFO changed{};
    changed.FileAttributes = attributes == 0 ? FILE_ATTRIBUTE_NORMAL : attributes;
    return ::SetFileInformationByHandle(handle, FileBasicInfo, &changed, sizeof(changed));
}

std::expected<DWORD, std::error_code> reparse_tag(HANDLE handle, DWORD attributes) {
    if(!(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return 0;
    }
    FILE_ATTRIBUTE_TAG_INFO info;
    if(!::GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info))) {
        return std::unexpected(windows::last_error());
    }
    return info.ReparseTag;
}

std::expected<FileID, std::error_code> file_id(HANDLE handle) {
    FILE_ID_INFO id;
    if(::GetFileInformationByHandleEx(handle, FileIdInfo, &id, sizeof(id))) {
        return id_of(id.VolumeSerialNumber, id.FileId);
    }
    BY_HANDLE_FILE_INFORMATION info;
    if(!::GetFileInformationByHandle(handle, &info)) {
        return std::unexpected(windows::last_error());
    }
    return FileID{.device = info.dwVolumeSerialNumber,
                  .file = (std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow};
}

}  // namespace kotatsu::fs::win32

#endif
