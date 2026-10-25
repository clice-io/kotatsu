#include "kota/fs/directory.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "kota/fs/path.h"

#ifdef _WIN32
#include <atomic>
#include <concepts>

#include "win32.h"
#include "kota/support/utf16.h"
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include "posix.h"
#endif

namespace kota::fs {

namespace {

#ifdef _WIN32

/// FILE_DISPOSITION_INFO_EX and FileRenameInfoEx's FILE_RENAME_INFO, which
/// SDKs before Windows 10 1607 and some MinGW headers lack.
struct DispositionInfoEx {
    ULONG Flags;
};

constexpr auto disposition_info_ex = static_cast<FILE_INFO_BY_HANDLE_CLASS>(21);
constexpr ULONG dispose_delete = 0x1;
constexpr ULONG dispose_posix_semantics = 0x2;
constexpr ULONG dispose_ignore_readonly = 0x10;

struct RenameInfoEx {
    ULONG Flags;
    HANDLE RootDirectory;
    DWORD FileNameLength;
    WCHAR FileName[1];
};

constexpr auto rename_info_ex = static_cast<FILE_INFO_BY_HANDLE_CLASS>(22);
constexpr ULONG rename_replace = 0x1;
constexpr ULONG rename_posix_semantics = 0x2;
constexpr ULONG rename_ignore_readonly = 0x40;

/// Whether a file system refused an information class it does not know,
/// one newer than it: FAT knows no POSIX deletes or renames, some network
/// file systems no extended directory listing.
bool unsupported(DWORD error) {
    return error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_SUPPORTED ||
           error == ERROR_INVALID_FUNCTION || error == ERROR_INVALID_LEVEL;
}

/// Hands each entry the directory `handle` lists, `.` and `..` left out, to
/// `visit` with its UTF-16 name, as the information class `Info` describes
/// it. The error that ended the listing early, or ERROR_SUCCESS.
template <typename Info, typename Visit>
DWORD for_each_entry(HANDLE handle, Visit visit) {
    constexpr bool extended = std::same_as<Info, FILE_ID_EXTD_DIR_INFO>;
    auto kind = extended ? FileIdExtdDirectoryRestartInfo : FileFullDirectoryRestartInfo;
    constexpr std::size_t buffer_size = 64 * 1024;
    // The entries hold 8-byte fields.
    auto buffer = std::make_unique<std::uint64_t[]>(buffer_size / sizeof(std::uint64_t));
    while(::GetFileInformationByHandleEx(handle, kind, buffer.get(), buffer_size)) {
        kind = extended ? FileIdExtdDirectoryInfo : FileFullDirectoryInfo;
        auto* at = reinterpret_cast<const char*>(buffer.get());
        while(true) {
            const auto& entry = *reinterpret_cast<const Info*>(at);
            std::wstring_view name(entry.FileName, entry.FileNameLength / sizeof(wchar_t));
            if(name != L"." && name != L"..") {
                visit(entry, name);
            }
            if(entry.NextEntryOffset == 0) {
                break;
            }
            at += entry.NextEntryOffset;
        }
    }
    // A file system that lists no `.` may report an empty directory as one
    // where no file matches.
    auto error = ::GetLastError();
    return error == ERROR_NO_MORE_FILES || error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
}

/// A handle to list the directory `dir` through. The access to list a
/// directory reads a file as well, so a file is told apart here.
std::expected<win32::Handle, std::error_code> open_directory(std::string_view dir) {
    auto handle = win32::open(dir, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES);
    if(!handle) {
        return handle;
    }
    FILE_BASIC_INFO info;
    if(!::GetFileInformationByHandleEx(handle->get(), FileBasicInfo, &info, sizeof(info))) {
        return std::unexpected(windows::last_error());
    }
    if(!(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return std::unexpected(std::make_error_code(std::errc::not_a_directory));
    }
    return handle;
}

/// Deletes what `handle`, opened for DELETE, refers to: by POSIX semantics,
/// so that the name goes even while others hold it open, and a read-only
/// file too; where the file system has none (FAT), the classic way.
DWORD dispose(HANDLE handle) {
    DispositionInfoEx posix{dispose_delete | dispose_posix_semantics | dispose_ignore_readonly};
    if(::SetFileInformationByHandle(handle, disposition_info_ex, &posix, sizeof(posix))) {
        return ERROR_SUCCESS;
    }
    if(auto error = ::GetLastError(); !unsupported(error)) {
        return error;
    }
    FILE_DISPOSITION_INFO classic{TRUE};
    if(::SetFileInformationByHandle(handle, FileDispositionInfo, &classic, sizeof(classic))) {
        return ERROR_SUCCESS;
    }
    return ::GetLastError();
}

/// Whether an open failed because nothing is there: a missing name, or a
/// component that is a file rather than a directory.
bool names_nothing(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

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
using NtStatusToErrorFn = ULONG(NTAPI*)(NTSTATUS);

/// NtCreateFile's disposition and options, which only the DDK names.
constexpr ULONG nt_open_existing = 1;
constexpr ULONG nt_synchronous_io = 0x20;
constexpr ULONG nt_backup_intent = 0x4000;
constexpr ULONG nt_open_reparse_point = 0x200000;

/// The function `name` of ntdll, which every Windows has, looked up once.
template <typename Function>
Function from_ntdll(std::atomic<Function>& cache, const char* name) {
    auto function = cache.load(std::memory_order_relaxed);
    if(function == nullptr) {
        auto found = ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), name);
        function = reinterpret_cast<Function>(reinterpret_cast<void (*)()>(found));
        cache.store(function, std::memory_order_relaxed);
    }
    return function;
}

/// The entry `name` of the directory `parent`, itself when it is a link,
/// opened through the parent's handle: Win32 opens names only from the
/// root, through directories a rename may have replaced since. An invalid
/// handle on failure, GetLastError() telling why.
win32::Handle open_entry(HANDLE parent, std::wstring_view name, DWORD access) {
    static constinit std::atomic<NtCreateFileFn> create = nullptr;
    static constinit std::atomic<NtStatusToErrorFn> to_error = nullptr;
    UNICODE_STRING object_name;
    object_name.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    object_name.MaximumLength = object_name.Length;
    object_name.Buffer = const_cast<PWSTR>(name.data());
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parent;
    attributes.ObjectName = &object_name;
    IO_STATUS_BLOCK status_block{};
    HANDLE handle = INVALID_HANDLE_VALUE;
    auto status = from_ntdll(create, "NtCreateFile")(
        &handle,
        access | SYNCHRONIZE,
        &attributes,
        &status_block,
        nullptr,
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nt_open_existing,
        nt_synchronous_io | nt_backup_intent | nt_open_reparse_point,
        nullptr,
        0);
    if(status < 0) {
        ::SetLastError(from_ntdll(to_error, "RtlNtStatusToDosError")(status));
        return win32::Handle();
    }
    return win32::Handle(handle);
}

/// Opens what remove_all() removes through `open(access)`: to list and
/// delete it, or only to delete it when the caller may not read it or
/// another process holds it open for everything but reading.
template <typename Open>
win32::Handle open_to_remove(Open open) {
    auto handle = open(DELETE | FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY);
    if(handle.get() == INVALID_HANDLE_VALUE) {
        if(auto error = ::GetLastError();
           error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION) {
            handle = open(DELETE | FILE_READ_ATTRIBUTES);
        }
    }
    return handle;
}

/// Removes what `handle`, from open_to_remove(), refers to, and for a
/// directory everything in it. The handle that decides whether to list an
/// entry lists it, opens each entry inside, and deletes it, so that no
/// rename meanwhile makes the walk follow a link: a link put in a
/// directory's place goes as the link, and a directory moved away goes
/// with what it held.
std::expected<std::uint64_t, std::error_code> remove_opened(const win32::Handle& handle) {
    FILE_ATTRIBUTE_TAG_INFO info;
    if(!::GetFileInformationByHandleEx(handle.get(), FileAttributeTagInfo, &info, sizeof(info))) {
        return std::unexpected(windows::last_error());
    }
    std::uint64_t removed = 0;
    if(win32::type_of(info.FileAttributes, info.ReparseTag) == FileType::Directory) {
        std::vector<std::wstring> names;
        auto error = for_each_entry<FILE_FULL_DIR_INFO>(
            handle.get(),
            [&](const FILE_FULL_DIR_INFO&, std::wstring_view name) { names.emplace_back(name); });
        if(error != ERROR_SUCCESS) {
            return std::unexpected(windows::to_error_code(error));
        }
        for(const auto& name: names) {
            auto child = open_to_remove(
                [&](DWORD access) { return open_entry(handle.get(), name, access); });
            if(child.get() == INVALID_HANDLE_VALUE) {
                // Gone since it was listed.
                if(names_nothing(::GetLastError())) {
                    continue;
                }
                return std::unexpected(windows::last_error());
            }
            auto count = remove_opened(child);
            if(!count) {
                return count;
            }
            removed += *count;
        }
    }
    if(auto error = dispose(handle.get()); error != ERROR_SUCCESS) {
        return std::unexpected(windows::to_error_code(error));
    }
    return removed + 1;
}

#else

/// The type a directory entry's d_type tells, or Unknown when it does not.
FileType type_of(unsigned char type) {
    switch(type) {
        case DT_REG: return FileType::Regular;
        case DT_DIR: return FileType::Directory;
        case DT_LNK: return FileType::Symlink;
        case DT_CHR: return FileType::Character;
        case DT_BLK: return FileType::Block;
        case DT_FIFO: return FileType::Fifo;
        case DT_SOCK: return FileType::Socket;
        default: return FileType::Unknown;
    }
}

/// Hands each entry of the open directory `dir`, `.` and `..` left out, to
/// `visit` with its name and d_type. readdir() reports an error only through
/// errno.
template <typename Visit>
std::error_code for_each_entry(DIR* dir, Visit visit) {
    while(true) {
        errno = 0;
        auto* entry = ::readdir(dir);
        if(entry == nullptr) {
            return errno == 0 ? std::error_code() : posix::last_error();
        }
        std::string_view name = entry->d_name;
        if(name != "." && name != "..") {
            visit(name, entry->d_type);
        }
    }
}

struct DirCloser {
    void operator()(DIR* dir) const {
        ::closedir(dir);
    }
};

using DirHandle = std::unique_ptr<DIR, DirCloser>;

std::expected<DirHandle, std::error_code> open_dir(std::string_view path) {
    auto c_path = posix::c_path(path);
    if(!c_path) {
        return std::unexpected(c_path.error());
    }
    DirHandle dir(::opendir(c_path->c_str()));
    if(!dir) {
        return std::unexpected(posix::last_error());
    }
    return dir;
}

/// Whether a call failed because nothing is there: a missing name, or a
/// component that is a file rather than a directory.
bool names_nothing(int error) {
    return error == ENOENT || error == ENOTDIR;
}

std::error_code
    remove_entry(int dir_fd, const char* name, unsigned char type, std::uint64_t& removed);

/// Removes everything in the open directory `fd`, adding what it removed to
/// `removed`.
std::error_code remove_contents(int fd, std::uint64_t& removed) {
    // fdopendir() takes the descriptor it is given. The names are all read
    // before any is removed, so a level of the walk holds one descriptor.
    int listed = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if(listed < 0) {
        return posix::last_error();
    }
    DirHandle dir(::fdopendir(listed));
    if(!dir) {
        auto error = posix::last_error();
        ::close(listed);
        return error;
    }
    std::vector<std::pair<std::string, unsigned char>> entries;
    auto error = for_each_entry(dir.get(), [&](std::string_view name, unsigned char type) {
        entries.emplace_back(name, type);
    });
    dir.reset();
    if(error) {
        return error;
    }
    for(auto& [name, type]: entries) {
        if(auto failed = remove_entry(fd, name.c_str(), type, removed)) {
            return failed;
        }
    }
    return {};
}

/// Removes the entry `name` of the directory `dir_fd` and everything in it,
/// by names relative to the directory each is in, never following a link:
/// a directory replaced by a link meanwhile goes as the link. `type` is its
/// d_type, DT_UNKNOWN for one not listed.
std::error_code
    remove_entry(int dir_fd, const char* name, unsigned char type, std::uint64_t& removed) {
    bool directory = type == DT_DIR;
    if(type == DT_UNKNOWN) {
        struct stat info;
        if(::fstatat(dir_fd, name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
            return names_nothing(errno) ? std::error_code() : posix::last_error();
        }
        directory = S_ISDIR(info.st_mode);
    }
    if(directory) {
        int fd = ::openat(dir_fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if(fd >= 0) {
            auto error = remove_contents(fd, removed);
            ::close(fd);
            if(error) {
                return error;
            }
        } else if(errno == ENOENT) {
            return {};
        } else if(errno == ENOTDIR || errno == ELOOP) {
            // No longer a directory: it goes as a file.
            directory = false;
        } else {
            return posix::last_error();
        }
    }
    if(::unlinkat(dir_fd, name, directory ? AT_REMOVEDIR : 0) == 0) {
        ++removed;
    } else if(errno != ENOENT) {
        return posix::last_error();
    }
    return {};
}

/// `path` without its trailing separators, its root kept: through them the
/// system follows a symlink the path ends in, and would remove what it
/// points to.
std::string_view without_trailing_separators(std::string_view path) {
    auto end = path.size();
    while(end > 1 && path[end - 1] == '/') {
        --end;
    }
    return path.substr(0, end);
}

#endif

std::error_code create_link(std::string_view target, std::string_view link, bool directory) {
#ifdef _WIN32
    auto native = windows::native_path(link);
    if(!native) {
        return native.error();
    }
    // The target stays as written, relative or not; only `\` separates in it.
    auto wide = win32::widen(path::to_backslash(target));
    if(!wide) {
        return wide.error();
    }
    DWORD flags = directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
    // Developer mode lets anyone create a link, but older systems reject the
    // flag that asks for it.
    constexpr DWORD unprivileged = 0x2;
    if(::CreateSymbolicLinkW(native->c_str(), wide->c_str(), flags | unprivileged)) {
        return {};
    }
    if(::GetLastError() == ERROR_INVALID_PARAMETER &&
       ::CreateSymbolicLinkW(native->c_str(), wide->c_str(), flags)) {
        return {};
    }
    return windows::last_error();
#else
    // POSIX tells a link to a directory by its target alone.
    static_cast<void>(directory);
    auto c_target = posix::c_path(target);
    if(!c_target) {
        return c_target.error();
    }
    auto c_link = posix::c_path(link);
    if(!c_link) {
        return c_link.error();
    }
    return ::symlink(c_target->c_str(), c_link->c_str()) == 0 ? std::error_code()
                                                              : posix::last_error();
#endif
}

}  // namespace

std::expected<std::vector<Entry>, std::error_code> read_dir(std::string_view dir) {
    std::vector<Entry> entries;
#ifdef _WIN32
    auto handle = open_directory(dir);
    if(!handle) {
        return std::unexpected(handle.error());
    }
    auto error = for_each_entry<FILE_FULL_DIR_INFO>(
        handle->get(),
        [&](const FILE_FULL_DIR_INFO& entry, std::wstring_view name) {
            // A reparse point's tag stands where the size of its extended
            // attributes would.
            entries.push_back(
                {from_utf16(name), win32::type_of(entry.FileAttributes, entry.EaSize)});
        });
    if(error != ERROR_SUCCESS) {
        return std::unexpected(windows::to_error_code(error));
    }
#else
    auto opened = open_dir(dir);
    if(!opened) {
        return std::unexpected(opened.error());
    }
    auto* handle = opened->get();
    auto error = for_each_entry(handle, [&](std::string_view name, unsigned char type) {
        Entry entry{std::string(name), type_of(type)};
        struct stat info;
        if(entry.type == FileType::Unknown &&
           ::fstatat(::dirfd(handle), entry.name.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0) {
            entry.type = posix::make_status(info).type;
        }
        entries.push_back(std::move(entry));
    });
    if(error) {
        return std::unexpected(error);
    }
#endif
    return entries;
}

std::expected<std::vector<StatusEntry>, std::error_code> read_dir_status(std::string_view dir) {
    std::vector<StatusEntry> entries;
#ifdef _WIN32
    auto handle = open_directory(dir);
    if(!handle) {
        return std::unexpected(handle.error());
    }
    // The listing tells each file's ID on the directory's volume.
    auto volume = win32::file_id(handle->get());
    if(!volume) {
        return std::unexpected(volume.error());
    }
    auto error = for_each_entry<FILE_ID_EXTD_DIR_INFO>(
        handle->get(),
        [&](const FILE_ID_EXTD_DIR_INFO& entry, std::wstring_view name) {
            entries.push_back({
                .name = from_utf16(name),
                .status = {.type = win32::type_of(entry.FileAttributes, entry.ReparsePointTag),
                           .size = static_cast<std::uint64_t>(entry.EndOfFile.QuadPart),
                           .mtime_ns = win32::unix_ns(entry.LastWriteTime.QuadPart),
                           .ctime_ns = win32::unix_ns(entry.ChangeTime.QuadPart),
                           .id = win32::id_of(volume->device, entry.FileID)},
            });
        });
    if(error == ERROR_SUCCESS) {
        return entries;
    }
    if(!unsupported(error)) {
        return std::unexpected(windows::to_error_code(error));
    }
    // A file system that lists no IDs: a status of each entry.
    auto listed = read_dir(dir);
    if(!listed) {
        return std::unexpected(listed.error());
    }
    entries.clear();
    for(auto& entry: *listed) {
        auto found = symlink_status(path::join(dir, entry.name));
        if(found) {
            entries.push_back({.name = std::move(entry.name), .status = *found});
        } else if(found.error() != std::errc::no_such_file_or_directory) {
            return std::unexpected(found.error());
        }
    }
#else
    auto opened = open_dir(dir);
    if(!opened) {
        return std::unexpected(opened.error());
    }
    int fd = ::dirfd(opened->get());
    std::error_code failed;
    auto error = for_each_entry(opened->get(), [&](std::string_view name, unsigned char) {
        struct stat info;
        std::string owned(name);
        if(::fstatat(fd, owned.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0) {
            entries.push_back({.name = std::move(owned), .status = posix::make_status(info)});
        } else if(errno != ENOENT && !failed) {
            // Gone since it was listed is no error.
            failed = posix::last_error();
        }
    });
    if(error || failed) {
        return std::unexpected(error ? error : failed);
    }
#endif
    return entries;
}

std::expected<bool, std::error_code> create_directory(std::string_view path) {
#ifdef _WIN32
    auto native = windows::native_path(path);
    if(!native) {
        return std::unexpected(native.error());
    }
    if(::CreateDirectoryW(native->c_str(), nullptr)) {
        return true;
    }
    auto error = windows::last_error();
#else
    auto c_path = posix::c_path(path);
    if(!c_path) {
        return std::unexpected(c_path.error());
    }
    if(::mkdir(c_path->c_str(), 0777) == 0) {
        return true;
    }
    auto error = posix::last_error();
#endif
    // A directory there already is what was asked for; a drive's root
    // refuses with access denied rather than saying it exists.
    if(is_directory(path)) {
        return false;
    }
    return std::unexpected(error);
}

std::expected<bool, std::error_code> create_directories(std::string_view path) {
    auto made = create_directory(path);
    if(made || made.error() != std::errc::no_such_file_or_directory) {
        return made;
    }
    auto above = path::parent(path);
    if(above.empty()) {
        return made;
    }
    auto parents = create_directories(above);
    if(!parents) {
        // A file where a directory above should be, which POSIX says of the
        // path below it.
        if(parents.error() == std::errc::file_exists) {
            return std::unexpected(std::make_error_code(std::errc::not_a_directory));
        }
        return parents;
    }
    auto last = create_directory(path);
    if(!last) {
        return last;
    }
    return *last || *parents;
}

std::expected<bool, std::error_code> remove(std::string_view path) {
#ifdef _WIN32
    auto native = windows::native_path(path);
    if(!native) {
        return std::unexpected(native.error());
    }
    auto handle = win32::try_open(*native, DELETE, FILE_FLAG_OPEN_REPARSE_POINT);
    if(handle.get() == INVALID_HANDLE_VALUE) {
        auto error = ::GetLastError();
        if(names_nothing(error)) {
            return false;
        }
        return std::unexpected(windows::to_error_code(error));
    }
    if(auto error = dispose(handle.get()); error != ERROR_SUCCESS) {
        return std::unexpected(windows::to_error_code(error));
    }
    return true;
#else
    auto c_path = posix::c_path(without_trailing_separators(path));
    if(!c_path) {
        return std::unexpected(c_path.error());
    }
    if(::unlink(c_path->c_str()) == 0) {
        return true;
    }
    auto error = errno;
    if(names_nothing(error)) {
        return false;
    }
    // A directory: Linux says EISDIR, macOS EPERM, which a file may give for
    // want of permission too.
    if(error == EISDIR || error == EPERM) {
        if(::rmdir(c_path->c_str()) == 0) {
            return true;
        }
        // ENOTDIR: a file after all, which the first error is about.
        if(errno == ENOENT) {
            return false;
        }
        if(errno != ENOTDIR) {
            return std::unexpected(posix::last_error());
        }
    }
    return std::unexpected(std::error_code(error, std::generic_category()));
#endif
}

std::expected<std::uint64_t, std::error_code> remove_all(std::string_view path) {
    // Removing `.` or `..` would remove the directory a path is in, which
    // the OS refuses only once its contents are gone.
    if(auto name = path::filename(path); name == "." || name == "..") {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
#ifdef _WIN32
    auto native = windows::native_path(path);
    if(!native) {
        return std::unexpected(native.error());
    }
    auto handle = open_to_remove([&](DWORD access) {
        return win32::try_open(*native, access, FILE_FLAG_OPEN_REPARSE_POINT);
    });
    if(handle.get() == INVALID_HANDLE_VALUE) {
        if(names_nothing(::GetLastError())) {
            return 0;
        }
        return std::unexpected(windows::last_error());
    }
    return remove_opened(handle);
#else
    auto c_path = posix::c_path(without_trailing_separators(path));
    if(!c_path) {
        return std::unexpected(c_path.error());
    }
    std::uint64_t removed = 0;
    if(auto error = remove_entry(AT_FDCWD, c_path->c_str(), DT_UNKNOWN, removed)) {
        return std::unexpected(error);
    }
    return removed;
#endif
}

std::error_code rename(std::string_view from, std::string_view to) {
#ifdef _WIN32
    auto source = windows::native_path(from);
    if(!source) {
        return source.error();
    }
    auto target = windows::native_path(to);
    if(!target) {
        return target.error();
    }
    auto handle = win32::try_open(*source, DELETE | SYNCHRONIZE, FILE_FLAG_OPEN_REPARSE_POINT);
    if(handle.get() == INVALID_HANDLE_VALUE) {
        return windows::last_error();
    }
    auto name_bytes = target->size() * sizeof(wchar_t);
    std::vector<std::byte> buffer(offsetof(RenameInfoEx, FileName) + name_bytes + sizeof(wchar_t));
    auto* info = reinterpret_cast<RenameInfoEx*>(buffer.data());
    info->Flags = rename_replace | rename_posix_semantics | rename_ignore_readonly;
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(name_bytes);
    std::memcpy(buffer.data() + offsetof(RenameInfoEx, FileName),
                target->c_str(),
                name_bytes + sizeof(wchar_t));
    if(::SetFileInformationByHandle(handle.get(),
                                    rename_info_ex,
                                    info,
                                    static_cast<DWORD>(buffer.size()))) {
        return {};
    }
    if(auto error = ::GetLastError(); !unsupported(error)) {
        return windows::to_error_code(error);
    }
    // A file system without POSIX renames (FAT).
    handle = win32::Handle();
    if(::MoveFileExW(source->c_str(), target->c_str(), MOVEFILE_REPLACE_EXISTING)) {
        return {};
    }
    return windows::last_error();
#else
    auto source = posix::c_path(from);
    if(!source) {
        return source.error();
    }
    auto target = posix::c_path(to);
    if(!target) {
        return target.error();
    }
    return ::rename(source->c_str(), target->c_str()) == 0 ? std::error_code()
                                                           : posix::last_error();
#endif
}

std::error_code create_symlink(std::string_view target, std::string_view link) {
    return create_link(target, link, false);
}

std::error_code create_directory_symlink(std::string_view target, std::string_view link) {
    return create_link(target, link, true);
}

}  // namespace kota::fs
