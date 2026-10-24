#include "kota/fs/directory.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "kota/fs/path.h"

#ifdef _WIN32
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
/// `visit` with its name, as the information class `Info` describes it. The
/// error that ended the listing early, or ERROR_SUCCESS.
template <typename Info, typename Visit>
DWORD for_each_entry(HANDLE handle,
                     FILE_INFO_BY_HANDLE_CLASS restart,
                     FILE_INFO_BY_HANDLE_CLASS next,
                     Visit visit) {
    constexpr std::size_t buffer_size = 64 * 1024;
    // The entries hold 8-byte fields.
    auto buffer = std::make_unique<std::uint64_t[]>(buffer_size / sizeof(std::uint64_t));
    auto kind = restart;
    while(::GetFileInformationByHandleEx(handle, kind, buffer.get(), buffer_size)) {
        kind = next;
        auto* at = reinterpret_cast<const char*>(buffer.get());
        while(true) {
            const auto& entry = *reinterpret_cast<const Info*>(at);
            std::wstring_view name(entry.FileName, entry.FileNameLength / sizeof(wchar_t));
            if(name != L"." && name != L"..") {
                visit(entry, from_utf16(name));
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

/// remove_all() of a verbatim path. The handle that decides whether to list
/// an entry is the one that lists it and the one it is deleted through,
/// opened on the entry itself, so that a link put in its place meanwhile is
/// deleted, never followed.
std::expected<std::uint64_t, std::error_code> remove_tree(const std::wstring& path) {
    constexpr DWORD flags = FILE_FLAG_OPEN_REPARSE_POINT;
    auto handle = win32::try_open(path, DELETE | FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY, flags);
    if(handle.get() == INVALID_HANDLE_VALUE && ::GetLastError() == ERROR_ACCESS_DENIED) {
        // A file the caller may delete but not read.
        handle = win32::try_open(path, DELETE | FILE_READ_ATTRIBUTES, flags);
    }
    if(handle.get() == INVALID_HANDLE_VALUE) {
        auto error = ::GetLastError();
        if(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return 0;
        }
        return std::unexpected(windows::to_error_code(error));
    }
    FILE_ATTRIBUTE_TAG_INFO info;
    if(!::GetFileInformationByHandleEx(handle.get(), FileAttributeTagInfo, &info, sizeof(info))) {
        return std::unexpected(windows::last_error());
    }
    std::uint64_t removed = 0;
    if(win32::type_of(info.FileAttributes, info.ReparseTag) == FileType::Directory) {
        std::vector<std::string> names;
        auto error = for_each_entry<FILE_FULL_DIR_INFO>(
            handle.get(),
            FileFullDirectoryRestartInfo,
            FileFullDirectoryInfo,
            [&](const FILE_FULL_DIR_INFO&, std::string name) { names.push_back(std::move(name)); });
        if(error != ERROR_SUCCESS) {
            return std::unexpected(windows::to_error_code(error));
        }
        for(auto& name: names) {
            auto count = remove_tree(path + L"\\" + *to_utf16<wchar_t>(name));
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

std::error_code create_link(std::string_view target, std::string_view link, DWORD flags) {
    auto native = windows::native_path(link);
    if(!native) {
        return native.error();
    }
    // The target stays as written, relative or not; only `\` separates in it.
    auto wide = to_utf16<wchar_t>(path::to_backslash(target));
    if(!wide) {
        return wide.error();
    }
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

/// Removes everything in the directory `fd`, by names relative to it, so
/// that a directory renamed or replaced by a link meanwhile is never left
/// for what it points to. Adds what it removed to `removed`.
std::error_code remove_contents(int fd, std::uint64_t& removed) {
    // fdopendir() takes the descriptor it is given.
    int listed = ::dup(fd);
    if(listed < 0) {
        return posix::last_error();
    }
    DirHandle dir(::fdopendir(listed));
    if(!dir) {
        ::close(listed);
        return posix::last_error();
    }
    std::vector<std::pair<std::string, unsigned char>> entries;
    if(auto error = for_each_entry(dir.get(), [&](std::string_view name, unsigned char type) {
           entries.emplace_back(name, type);
       })) {
        return error;
    }
    for(auto& [name, type]: entries) {
        bool directory = type == DT_DIR;
        if(type == DT_UNKNOWN) {
            struct stat info;
            directory = ::fstatat(fd, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0 &&
                        S_ISDIR(info.st_mode);
        }
        if(directory) {
            int child = ::openat(fd, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if(child < 0) {
                if(errno == ENOENT) {
                    continue;
                }
                if(errno != ENOTDIR && errno != ELOOP) {
                    return posix::last_error();
                }
                // No longer a directory: removed as a file below.
                directory = false;
            } else {
                auto error = remove_contents(child, removed);
                ::close(child);
                if(error) {
                    return error;
                }
            }
        }
        if(::unlinkat(fd, name.c_str(), directory ? AT_REMOVEDIR : 0) == 0) {
            ++removed;
        } else if(errno != ENOENT) {
            return posix::last_error();
        }
    }
    return {};
}

std::error_code create_link(std::string_view target, std::string_view link) {
    auto c_target = posix::c_path(target);
    auto c_link = posix::c_path(link);
    if(!c_target || !c_link) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    return ::symlink(c_target->c_str(), c_link->c_str()) == 0 ? std::error_code()
                                                              : posix::last_error();
}

#endif

}  // namespace

std::expected<std::vector<Entry>, std::error_code> read_dir(std::string_view dir) {
    std::vector<Entry> entries;
#ifdef _WIN32
    auto handle = win32::open(dir, FILE_LIST_DIRECTORY, 0);
    if(!handle) {
        return std::unexpected(handle.error());
    }
    auto error = for_each_entry<FILE_FULL_DIR_INFO>(
        handle->get(),
        FileFullDirectoryRestartInfo,
        FileFullDirectoryInfo,
        [&](const FILE_FULL_DIR_INFO& entry, std::string name) {
            // A reparse point's tag stands where the size of its extended
            // attributes would.
            entries.push_back(
                {std::move(name), win32::type_of(entry.FileAttributes, entry.EaSize)});
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
        auto known = type_of(type);
        if(known == FileType::Unknown) {
            struct stat info;
            if(::fstatat(::dirfd(handle), std::string(name).c_str(), &info, AT_SYMLINK_NOFOLLOW) ==
               0) {
                known = posix::make_status(info).type;
            }
        }
        entries.push_back({std::string(name), known});
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
    auto handle = win32::open(dir, FILE_LIST_DIRECTORY, 0);
    if(!handle) {
        return std::unexpected(handle.error());
    }
    // The listing tells each file's ID on the directory's volume.
    std::uint64_t volume = 0;
    FILE_ID_INFO id;
    BY_HANDLE_FILE_INFORMATION info;
    if(::GetFileInformationByHandleEx(handle->get(), FileIdInfo, &id, sizeof(id))) {
        volume = id.VolumeSerialNumber;
    } else if(::GetFileInformationByHandle(handle->get(), &info)) {
        volume = info.dwVolumeSerialNumber;
    } else {
        return std::unexpected(windows::last_error());
    }
    auto error = for_each_entry<FILE_ID_EXTD_DIR_INFO>(
        handle->get(),
        FileIdExtdDirectoryRestartInfo,
        FileIdExtdDirectoryInfo,
        [&](const FILE_ID_EXTD_DIR_INFO& entry, std::string name) {
            entries.push_back({
                .name = std::move(name),
                .status = {.type = win32::type_of(entry.FileAttributes, entry.ReparsePointTag),
                           .size = static_cast<std::uint64_t>(entry.EndOfFile.QuadPart),
                           .mtime_ns = win32::unix_ns(entry.LastWriteTime.QuadPart),
                           .ctime_ns = win32::unix_ns(entry.ChangeTime.QuadPart),
                           .id = win32::id_of(volume, entry.FileId)},
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
    if(above.empty() || above.size() == path.size()) {
        return made;
    }
    auto parents = create_directories(above);
    if(!parents) {
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
        if(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return false;
        }
        return std::unexpected(windows::to_error_code(error));
    }
    if(auto error = dispose(handle.get()); error != ERROR_SUCCESS) {
        return std::unexpected(windows::to_error_code(error));
    }
    return true;
#else
    auto c_path = posix::c_path(path);
    if(!c_path) {
        return std::unexpected(c_path.error());
    }
    if(::unlink(c_path->c_str()) == 0) {
        return true;
    }
    auto error = errno;
    if(error == ENOENT) {
        return false;
    }
    // A directory: Linux says EISDIR, macOS EPERM, which a file may give for
    // want of permission too.
    if(error == EISDIR || error == EPERM) {
        if(::rmdir(c_path->c_str()) == 0) {
            return true;
        }
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
#ifdef _WIN32
    auto native = windows::native_path(path);
    if(!native) {
        return std::unexpected(native.error());
    }
    return remove_tree(win32::verbatim(std::move(*native)));
#else
    auto found = symlink_status(path);
    if(!found) {
        if(found.error() == std::errc::no_such_file_or_directory) {
            return 0;
        }
        return std::unexpected(found.error());
    }
    std::uint64_t removed = 0;
    if(found->type == FileType::Directory) {
        int fd = ::open(std::string(path).c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if(fd >= 0) {
            auto error = remove_contents(fd, removed);
            ::close(fd);
            if(error) {
                return std::unexpected(error);
            }
        } else if(errno != ENOTDIR && errno != ELOOP) {
            return std::unexpected(posix::last_error());
        }
    }
    auto gone = remove(path);
    if(!gone) {
        return std::unexpected(gone.error());
    }
    return removed + *gone;
#endif
}

std::error_code rename(std::string_view from, std::string_view to) {
#ifdef _WIN32
    auto source = windows::native_path(from);
    auto target = windows::native_path(to);
    if(!source || !target) {
        return source ? target.error() : source.error();
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
    auto target = posix::c_path(to);
    if(!source || !target) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    return ::rename(source->c_str(), target->c_str()) == 0 ? std::error_code()
                                                           : posix::last_error();
#endif
}

std::error_code create_symlink(std::string_view target, std::string_view link) {
#ifdef _WIN32
    return create_link(target, link, 0);
#else
    return create_link(target, link);
#endif
}

std::error_code create_directory_symlink(std::string_view target, std::string_view link) {
#ifdef _WIN32
    return create_link(target, link, SYMBOLIC_LINK_FLAG_DIRECTORY);
#else
    return create_link(target, link);
#endif
}

}  // namespace kota::fs
