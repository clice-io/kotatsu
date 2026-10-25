#include "kota/fs/status.h"

#include <string>

#ifdef _WIN32
#include <atomic>
#include <ratio>

#include "win32.h"
#else
#include <fcntl.h>

#include "posix.h"
#endif

namespace kota::fs {

#ifdef _WIN32

std::expected<Status, std::error_code> win32::handle_status(HANDLE handle) {
    switch(::GetFileType(handle)) {
        case FILE_TYPE_DISK: break;
        case FILE_TYPE_CHAR: return Status{.type = FileType::Character};
        case FILE_TYPE_PIPE: return Status{.type = FileType::Fifo};
        default: {
            // FILE_TYPE_UNKNOWN, and an error only when one is set.
            if(auto error = ::GetLastError(); error != NO_ERROR) {
                return std::unexpected(windows::to_error_code(error));
            }
            return Status{.type = FileType::Unknown};
        }
    }
    FILE_BASIC_INFO basic;
    FILE_STANDARD_INFO standard;
    if(!::GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) ||
       !::GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard))) {
        return std::unexpected(windows::last_error());
    }
    DWORD tag = 0;
    if(basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        FILE_ATTRIBUTE_TAG_INFO info;
        if(!::GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info))) {
            return std::unexpected(windows::last_error());
        }
        tag = info.ReparseTag;
    }
    auto id = win32::file_id(handle);
    if(!id) {
        return std::unexpected(id.error());
    }
    return Status{
        .type = win32::type_of(basic.FileAttributes, tag),
        .size = static_cast<std::uint64_t>(standard.EndOfFile.QuadPart),
        .mtime = win32::unix_time(basic.LastWriteTime.QuadPart),
        .ctime = win32::unix_time(basic.ChangeTime.QuadPart),
        .id = *id,
        .links = standard.NumberOfLinks,
        .permissions = win32::permissions_of(basic.FileAttributes),
    };
}

#endif

namespace {

#ifdef _WIN32

/// The device characteristic of a file on another machine, which the user
/// mode headers lack.
constexpr ULONG remote_device = 0x10;

/// FILE_STAT_BASIC_INFORMATION, which only the newest SDKs declare.
struct StatBasicByName {
    LARGE_INTEGER FileId;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG FileAttributes;
    ULONG ReparseTag;
    ULONG NumberOfLinks;
    ULONG DeviceType;
    ULONG DeviceCharacteristics;
    ULONG Reserved;
    LARGE_INTEGER VolumeSerialNumber;
    FILE_ID_128 FileId128;
};

/// FileStatBasicByNameInfo, of FILE_INFO_BY_NAME_CLASS.
constexpr int stat_basic_by_name = 3;

using GetFileInformationByNameFn = BOOL(WINAPI*)(PCWSTR, int, PVOID, ULONG);

/// GetFileInformationByName, which Windows has from 11 24H2 on; null before.
GetFileInformationByNameFn by_name() {
    static constinit std::atomic<bool> resolved = false;
    static constinit std::atomic<GetFileInformationByNameFn> function = nullptr;
    if(!resolved.load(std::memory_order_acquire)) {
        auto found =
            ::GetProcAddress(::GetModuleHandleW(L"kernelbase.dll"), "GetFileInformationByName");
        function.store(
            reinterpret_cast<GetFileInformationByNameFn>(reinterpret_cast<void (*)()>(found)),
            std::memory_order_relaxed);
        resolved.store(true, std::memory_order_release);
    }
    return function.load(std::memory_order_relaxed);
}

/// The errors a by-name query reports only for a path that names nothing;
/// any other it may report for a file an open reaches.
bool names_nothing(DWORD error) {
    switch(error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_NOT_READY:
        case ERROR_BAD_NET_NAME:
        case ERROR_BAD_NETPATH:
        case ERROR_BAD_PATHNAME:
        case ERROR_INVALID_NAME: return true;
        default: return false;
    }
}

/// The status a directory's listing gives a file no open reaches: a page
/// file, a directory the caller may list but not open. It has no change
/// time, ID or links.
std::expected<Status, std::error_code> listed_status(win32::WidePath& path) {
    // A name with a wildcard would match others; Windows allows none.
    std::wstring_view spelled(path.data(), path.size());
    auto name_start = spelled.find_last_of(L"\\/");
    if(spelled.find_first_of(L"*?", name_start == std::wstring_view::npos ? 0 : name_start) !=
       std::wstring_view::npos) {
        return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    }
    WIN32_FIND_DATAW data;
    HANDLE find =
        ::FindFirstFileExW(path.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if(find == INVALID_HANDLE_VALUE) {
        return std::unexpected(windows::last_error());
    }
    ::FindClose(find);
    auto ticks = [](const FILETIME& time) {
        return static_cast<std::int64_t>((std::uint64_t(time.dwHighDateTime) << 32) |
                                         time.dwLowDateTime);
    };
    return Status{
        .type = win32::type_of(data.dwFileAttributes, data.dwReserved0),
        .size = (std::uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow,
        .mtime = win32::unix_time(ticks(data.ftLastWriteTime)),
        .permissions = win32::permissions_of(data.dwFileAttributes),
    };
}

std::expected<Status, std::error_code> status_of(std::string_view path, bool follow) {
    win32::WidePath native;
    if(auto error = windows::to_native(native, path)) {
        return std::unexpected(error);
    }
    if(auto query = by_name()) {
        StatBasicByName info;
        if(query(native.c_str(), stat_basic_by_name, &info, sizeof(info))) {
            // A link to follow takes an open, and so does a remote file: a
            // network file system answers the query with less care.
            bool link = info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT;
            if(!(info.DeviceCharacteristics & remote_device) && !(follow && link)) {
                return Status{
                    .type = win32::type_of(info.FileAttributes, info.ReparseTag),
                    .size = static_cast<std::uint64_t>(info.EndOfFile.QuadPart),
                    .mtime = win32::unix_time(info.LastWriteTime.QuadPart),
                    .ctime = win32::unix_time(info.ChangeTime.QuadPart),
                    .id = win32::id_of(info.VolumeSerialNumber.QuadPart, info.FileId128),
                    .links = info.NumberOfLinks,
                    .permissions = win32::permissions_of(info.FileAttributes),
                };
            }
        } else if(auto error = ::GetLastError(); names_nothing(error)) {
            return std::unexpected(windows::to_error_code(error));
        }
    }
    DWORD flags = follow ? 0 : FILE_FLAG_OPEN_REPARSE_POINT;
    auto handle = win32::try_open(native.c_str(), 0, flags);
    if(handle.get() != INVALID_HANDLE_VALUE) {
        return win32::handle_status(handle.get());
    }
    auto error = ::GetLastError();
    if(error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED) {
        auto listed = listed_status(native);
        if(listed && !(follow && listed->type == FileType::Symlink)) {
            return listed;
        }
    } else if(error == ERROR_CANT_ACCESS_FILE && follow) {
        // A reparse point no path is resolved through, which still cannot be
        // opened as a file: a Unix socket, an app execution alias.
        auto itself = win32::try_open(native.c_str(), 0, FILE_FLAG_OPEN_REPARSE_POINT);
        if(itself.get() != INVALID_HANDLE_VALUE) {
            auto result = win32::handle_status(itself.get());
            if(result && result->type != FileType::Symlink) {
                return result;
            }
        }
    }
    return std::unexpected(windows::to_error_code(error));
}

#else

std::expected<Status, std::error_code> status_of(std::string_view path, bool follow) {
    posix::CPath c_path;
    if(auto error = c_path.assign(path)) {
        return std::unexpected(error);
    }
    struct stat info;
    // A symlink the path ends in stays itself behind a separator, through
    // which the system would follow it, as Windows does.
    if(auto link = posix::without_trailing_separators(path); !follow && link != path) {
        // No NUL in it, as there was none in `path`.
        posix::CPath c_link;
        [[maybe_unused]] auto checked = c_link.assign(link);
        if(::lstat(c_link.c_str(), &info) == 0 && S_ISLNK(info.st_mode)) {
            return posix::make_status(info);
        }
    }
    if((follow ? ::stat(c_path.c_str(), &info) : ::lstat(c_path.c_str(), &info)) != 0) {
        return std::unexpected(posix::last_error());
    }
    return posix::make_status(info);
}

#endif

}  // namespace

std::expected<Status, std::error_code> status(std::string_view path) {
    return status_of(path, true);
}

std::expected<Status, std::error_code> symlink_status(std::string_view path) {
    return status_of(path, false);
}

std::expected<bool, std::error_code> exists(std::string_view path) {
#ifdef _WIN32
    // The attributes by name, which open nothing before Windows 11 24H2 as
    // status() would: a link to follow, or a file they do not reach, takes a
    // status after all.
    win32::WidePath native;
    if(auto error = windows::to_native(native, path)) {
        return std::unexpected(error);
    }
    auto attributes = ::GetFileAttributesW(native.c_str());
    if(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return true;
    }
#endif
    auto found = status(path);
    if(found) {
        return true;
    }
    if(found.error() == std::errc::no_such_file_or_directory ||
       found.error() == std::errc::not_a_directory) {
        return false;
    }
    return std::unexpected(found.error());
}

bool is_file(std::string_view path) {
    auto found = status(path);
    return found && found->type == FileType::Regular;
}

bool is_directory(std::string_view path) {
    auto found = status(path);
    return found && found->type == FileType::Directory;
}

bool is_symlink(std::string_view path) {
    auto found = symlink_status(path);
    return found && found->type == FileType::Symlink;
}

std::error_code set_times(std::string_view path, const FileTimes& times) {
#ifdef _WIN32
    // Every time nanoseconds since 1970 hold lies after 1601, FILETIME's
    // start, which SetFileTime would take for leaving a time as it is.
    auto file_time = [](const std::optional<std::chrono::sys_time<std::chrono::nanoseconds>>& time,
                        FILETIME& out) -> FILETIME* {
        if(!time) {
            return nullptr;
        }
        using Ticks = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;
        auto ticks = static_cast<std::uint64_t>(
            std::chrono::floor<Ticks>(time->time_since_epoch()).count() + win32::unix_epoch_ticks);
        out.dwLowDateTime = static_cast<DWORD>(ticks);
        out.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
        return &out;
    };
    auto handle = win32::open(path, FILE_WRITE_ATTRIBUTES);
    if(!handle) {
        return handle.error();
    }
    FILETIME accessed;
    FILETIME modified;
    if(!::SetFileTime(handle->get(),
                      nullptr,
                      file_time(times.accessed, accessed),
                      file_time(times.modified, modified))) {
        return windows::last_error();
    }
    return {};
#else
    auto spec_of = [](const std::optional<std::chrono::sys_time<std::chrono::nanoseconds>>& time) {
        if(!time) {
            return timespec{.tv_sec = 0, .tv_nsec = UTIME_OMIT};
        }
        auto since = time->time_since_epoch();
        auto seconds = std::chrono::floor<std::chrono::seconds>(since);
        return timespec{.tv_sec = static_cast<time_t>(seconds.count()),
                        .tv_nsec = static_cast<long>((since - seconds).count())};
    };
    posix::CPath c_path;
    if(auto error = c_path.assign(path)) {
        return error;
    }
    timespec specs[2] = {spec_of(times.accessed), spec_of(times.modified)};
    if(::utimensat(AT_FDCWD, c_path.c_str(), specs, 0) != 0) {
        return posix::last_error();
    }
    return {};
#endif
}

std::error_code set_permissions(std::string_view path, std::uint16_t permissions) {
#ifdef _WIN32
    auto handle = win32::open(path, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES);
    if(!handle) {
        return handle.error();
    }
    FILE_BASIC_INFO info;
    if(!::GetFileInformationByHandleEx(handle->get(), FileBasicInfo, &info, sizeof(info))) {
        return windows::last_error();
    }
    auto attributes = permissions & 0222 ? info.FileAttributes & ~FILE_ATTRIBUTE_READONLY
                                         : info.FileAttributes | FILE_ATTRIBUTE_READONLY;
    if(attributes != info.FileAttributes && !win32::set_attributes(handle->get(), attributes)) {
        return windows::last_error();
    }
    return {};
#else
    posix::CPath c_path;
    if(auto error = c_path.assign(path)) {
        return error;
    }
    if(::chmod(c_path.c_str(), static_cast<mode_t>(permissions & 07777)) != 0) {
        return posix::last_error();
    }
    return {};
#endif
}

std::expected<bool, std::error_code> equivalent(std::string_view a, std::string_view b) {
    auto first = status(a);
    if(!first) {
        return std::unexpected(first.error());
    }
    auto second = status(b);
    if(!second) {
        return std::unexpected(second.error());
    }
    // A device, a pipe and a file only a directory listing reaches have no
    // ID to tell them apart.
    if(first->id == FileID{} || second->id == FileID{}) {
        return std::unexpected(std::make_error_code(std::errc::not_supported));
    }
    return first->id == second->id;
}

}  // namespace kota::fs
