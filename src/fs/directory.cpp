#include "kota/fs/directory.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "handle.h"
#include "kota/support/small_string.h"
#include "kota/fs/path.h"

#ifdef _WIN32
#include "win32.h"
#include "kota/support/utf16.h"
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include "posix.h"

#ifdef __APPLE__
// renamex_np
#include <stdio.h>
#else
#include <sys/syscall.h>

// What C libraries before glibc 2.28, and their kernel headers before Linux
// 3.15, leave out: renameat2's flag, and its number on x86-64 and on the
// architectures of the generic table.
#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE 1
#endif
#ifndef SYS_renameat2
#if defined(__x86_64__)
#define SYS_renameat2 316
#elif defined(__aarch64__) || defined(__riscv)
#define SYS_renameat2 276
#endif
#endif
#endif
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

/// The name of a listed entry of the information class `Info`.
template <typename Info>
std::wstring_view name_of(const void* entry) {
    const auto& info = *static_cast<const Info*>(entry);
    return {info.FileName, info.FileNameLength / sizeof(wchar_t)};
}

/// What a listed entry of the information class `Info`, with the reparse
/// tag `tag`, tells of its status: no ID or links.
template <typename Info>
Status listed_status(const Info& info, DWORD tag) {
    return {
        .type = win32::type_of(info.FileAttributes, tag),
        .size = static_cast<std::uint64_t>(info.EndOfFile.QuadPart),
        .mtime = win32::unix_time(info.LastWriteTime.QuadPart),
        .ctime = win32::unix_time(info.ChangeTime.QuadPart),
        .permissions = win32::permissions_of(info.FileAttributes),
    };
}

/// The entries a directory handle lists, `.` and `..` left out, read a
/// buffer of them at a time: as FILE_ID_EXTD_DIR_INFO while `extended`,
/// otherwise as FILE_FULL_DIR_INFO.
struct Listing {
    /// The next entry `handle` lists, null at the end, or the error that
    /// ended the listing. A file system that lists no IDs (FAT) refuses the
    /// first read of FILE_ID_EXTD_DIR_INFO, which then lists the other.
    std::expected<const void*, DWORD> next(HANDLE handle) {
        while(true) {
            if(at == nullptr) {
                auto kind =
                    extended ? (started ? FileIdExtdDirectoryInfo : FileIdExtdDirectoryRestartInfo)
                             : (started ? FileFullDirectoryInfo : FileFullDirectoryRestartInfo);
                if(!::GetFileInformationByHandleEx(handle, kind, buffer.get(), buffer_size)) {
                    // A file system that lists no `.` may report an empty
                    // directory as one where no file matches.
                    auto error = ::GetLastError();
                    if(error == ERROR_NO_MORE_FILES || error == ERROR_FILE_NOT_FOUND) {
                        return nullptr;
                    }
                    if(extended && !started && unsupported(error)) {
                        extended = false;
                        continue;
                    }
                    return std::unexpected(error);
                }
                started = true;
                at = reinterpret_cast<const char*>(buffer.get());
            }
            const char* entry = at;
            auto offset =
                extended ? reinterpret_cast<const FILE_ID_EXTD_DIR_INFO*>(entry)->NextEntryOffset
                         : reinterpret_cast<const FILE_FULL_DIR_INFO*>(entry)->NextEntryOffset;
            at = offset == 0 ? nullptr : at + offset;
            auto name = extended ? name_of<FILE_ID_EXTD_DIR_INFO>(entry)
                                 : name_of<FILE_FULL_DIR_INFO>(entry);
            if(name != L"." && name != L"..") {
                return entry;
            }
        }
    }

    constexpr static std::size_t buffer_size = 64 * 1024;

    bool extended;

    /// Whether a buffer has been read, which the next read follows on from.
    bool started = false;

    /// The entries hold 8-byte fields.
    std::unique_ptr<std::uint64_t[]> buffer =
        std::make_unique<std::uint64_t[]>(buffer_size / sizeof(std::uint64_t));

    /// The next entry in the buffer, null once it is used up.
    const char* at = nullptr;
};

/// A handle to list the directory `dir` through. The access to list a
/// directory reads a file as well, so a file is told apart here.
std::expected<win32::Handle, std::error_code> open_directory(std::string_view dir) {
    auto handle = win32::open(dir, FILE_LIST_DIRECTORY);
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
/// file too; where the file system has none (FAT), the classic way, a
/// read-only file or directory without the attribute meanwhile.
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
    auto error = ::GetLastError();
    if(error != ERROR_ACCESS_DENIED) {
        return error;
    }
    // The attributes take a handle of their own, the same file reopened by
    // an empty name relative to it, which ReOpenFile refuses for a
    // directory.
    auto attributes = win32::open_at(handle, L"", FILE_WRITE_ATTRIBUTES, false);
    FILE_BASIC_INFO basic;
    if(!attributes ||
       !::GetFileInformationByHandleEx(attributes->get(), FileBasicInfo, &basic, sizeof(basic)) ||
       !(basic.FileAttributes & FILE_ATTRIBUTE_READONLY) ||
       !win32::set_attributes(attributes->get(), basic.FileAttributes & ~FILE_ATTRIBUTE_READONLY)) {
        return error;
    }
    if(::SetFileInformationByHandle(handle, FileDispositionInfo, &classic, sizeof(classic))) {
        return ERROR_SUCCESS;
    }
    error = ::GetLastError();
    win32::set_attributes(attributes->get(), basic.FileAttributes);
    return error;
}

/// A file open to change its attributes, with them, its ID and when it was
/// made and last written, which together tell it, as far as FAT can, from a
/// file that has taken its name since: FAT's ID is the place of the file's
/// entry, which a new file may take over, and a file made under a name that
/// went moments before takes over its creation time too (tunneling), though
/// not its write time.
struct AttributeHandle {
    win32::Handle handle;
    FILE_BASIC_INFO basic;
    FileID id;
};

/// What `path` names, a link itself, as an AttributeHandle; empty when it
/// cannot be opened or read.
std::optional<AttributeHandle> open_attributes(std::wstring_view path) {
    auto handle = win32::open_native(path, FILE_WRITE_ATTRIBUTES, false);
    FILE_BASIC_INFO basic;
    if(!handle ||
       !::GetFileInformationByHandleEx(handle->get(), FileBasicInfo, &basic, sizeof(basic))) {
        return std::nullopt;
    }
    auto id = win32::file_id(handle->get());
    if(!id) {
        return std::nullopt;
    }
    return AttributeHandle{.handle = std::move(*handle), .basic = basic, .id = *id};
}

/// Whether an open failed because nothing is there: a missing name, a
/// component that is a file rather than a directory, or a file deleted
/// while held open, which goes with the last handle.
bool names_nothing(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
           error == ERROR_DELETE_PENDING;
}

/// Opens what remove_all() removes through `open(access)`: to list and
/// delete it, or only to delete it when the caller may not read it or
/// another process holds it open for everything but reading.
template <typename Open>
std::expected<win32::Handle, DWORD> open_to_remove(Open open) {
    auto handle = open(DELETE | FILE_LIST_DIRECTORY);
    if(!handle &&
       (handle.error() == ERROR_ACCESS_DENIED || handle.error() == ERROR_SHARING_VIOLATION)) {
        return open(DELETE);
    }
    return handle;
}

/// Appends to `names` the names the directory `handle` lists. The listing's
/// buffer goes once they are read.
std::error_code list_names(std::vector<std::wstring>& names, HANDLE handle) {
    Listing listing{.extended = false};
    while(true) {
        auto entry = listing.next(handle);
        if(!entry) {
            return windows::to_error_code(entry.error());
        }
        if(*entry == nullptr) {
            return {};
        }
        names.emplace_back(name_of<FILE_FULL_DIR_INFO>(*entry));
    }
}

/// What remove_all() opened, held open until it goes, once what it lists,
/// if it is a directory, is gone; and where its names start among those
/// listed.
struct Level {
    win32::Handle handle;
    std::size_t names;
};

/// Removes what `handle`, from open_to_remove(), refers to, and for a
/// directory everything in it, a level at a time rather than a call deeper.
/// The handle that decides whether to list an entry lists it, opens each
/// entry inside, and deletes it, so that no rename meanwhile makes the walk
/// follow a link: a link put in a directory's place goes as the link, and a
/// directory moved away goes with what it held. Each level holds its
/// handle: Windows sets no limit on handles that a tree's depth comes near,
/// and FAT, which opens nothing by ID, offers no other way back up.
std::expected<std::uint64_t, std::error_code> remove_tree(win32::Handle handle) {
    std::uint64_t removed = 0;
    std::vector<std::wstring> names;
    std::vector<Level> levels;
    while(true) {
        FILE_BASIC_INFO info;
        if(!::GetFileInformationByHandleEx(handle.get(), FileBasicInfo, &info, sizeof(info))) {
            return std::unexpected(windows::last_error());
        }
        auto tag = win32::reparse_tag(handle.get(), info.FileAttributes);
        if(!tag) {
            return std::unexpected(tag.error());
        }
        auto start = names.size();
        if(win32::type_of(info.FileAttributes, *tag) == FileType::Directory) {
            if(auto error = list_names(names, handle.get())) {
                return std::unexpected(error);
            }
        }
        levels.push_back({.handle = std::move(handle), .names = start});
        // The next entry to remove, from the deepest level with any left;
        // one with none left goes.
        while(handle.get() == INVALID_HANDLE_VALUE) {
            if(levels.empty()) {
                return removed;
            }
            auto& level = levels.back();
            if(names.size() == level.names) {
                if(auto error = dispose(level.handle.get()); error != ERROR_SUCCESS) {
                    return std::unexpected(windows::to_error_code(error));
                }
                ++removed;
                // Closed before the directory it is in is deleted, which
                // FAT, deleting the classic way, refuses until then.
                levels.pop_back();
                continue;
            }
            auto name = std::move(names.back());
            names.pop_back();
            auto opened = open_to_remove([&](DWORD access) {
                return win32::open_at(level.handle.get(), name, access, false);
            });
            if(opened) {
                handle = std::move(*opened);
            } else if(!names_nothing(opened.error())) {
                // One that names nothing went since it was listed.
                return std::unexpected(windows::to_error_code(opened.error()));
            }
        }
    }
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

/// The next entry of the open directory `dir`, `.` and `..` left out, null
/// at the end. readdir() reports an error only through errno.
std::expected<const dirent*, std::error_code> next_entry(DIR* dir) {
    while(true) {
        errno = 0;
        const auto* entry = ::readdir(dir);
        if(entry == nullptr) {
            if(errno != 0) {
                return std::unexpected(posix::last_error());
            }
            return nullptr;
        }
        std::string_view name = entry->d_name;
        if(name != "." && name != "..") {
            return entry;
        }
    }
}

struct DirCloser {
    void operator()(DIR* dir) const {
        ::closedir(dir);
    }
};

using DirHandle = std::unique_ptr<DIR, DirCloser>;

/// A descriptor, closed when this goes; AT_FDCWD is none to close.
struct Descriptor {
    explicit Descriptor(int fd) : fd(fd) {}

    /// Takes `other`'s descriptor, which closes this one's when it goes.
    Descriptor& operator=(Descriptor&& other) noexcept {
        std::swap(fd, other.fd);
        return *this;
    }

    ~Descriptor() {
        if(fd >= 0) {
            ::close(fd);
        }
    }

    int fd;
};

std::expected<DirHandle, std::error_code> open_dir(std::string_view path) {
    posix::CPath c_path;
    if(auto error = c_path.assign(path)) {
        return std::unexpected(error);
    }
    DirHandle dir(::opendir(c_path.c_str()));
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

/// An entry remove_all() listed and has yet to remove, with its d_type,
/// DT_UNKNOWN for one not listed.
struct Listed {
    std::string name;
    unsigned char type;
};

/// Appends to `listed` the entries of the open directory `fd`.
std::error_code list_entries(std::vector<Listed>& listed, int fd) {
    // fdopendir() takes the descriptor it is given.
    int duplicate = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if(duplicate < 0) {
        return posix::last_error();
    }
    DirHandle dir(::fdopendir(duplicate));
    if(!dir) {
        auto error = posix::last_error();
        ::close(duplicate);
        return error;
    }
    while(true) {
        auto entry = next_entry(dir.get());
        if(!entry) {
            return entry.error();
        }
        if(*entry == nullptr) {
            return {};
        }
        listed.push_back({.name = (*entry)->d_name, .type = (*entry)->d_type});
    }
}

/// A directory remove_all() went into: its name in the one above, its
/// device and inode, which tell whether `..` leads back to it, and where
/// its entries start among those listed.
struct Level {
    std::string name;
    dev_t device;
    ino_t inode;
    std::size_t entries;
};

/// Removes `path` and everything in it, a level at a time rather than a
/// call deeper, by names relative to the directory each is in, never
/// following a link: a directory replaced by a link meanwhile goes as the
/// link. It holds open only the directory it removes from and climbs back
/// through `..`, so that a tree of any depth takes a few descriptors; a
/// directory that lists nothing goes from the one above without the walk
/// going in, since it could not climb out of one it may read but not
/// search.
std::expected<std::uint64_t, std::error_code> remove_tree(const char* path) {
    std::uint64_t removed = 0;
    std::vector<Listed> listed = {
        {.name = path, .type = DT_UNKNOWN}
    };
    std::vector<Level> levels;
    // Where entries go from: for `path` itself, the current directory.
    Descriptor dir(AT_FDCWD);
    while(true) {
        // What goes next, from `dir`: an entry listed, or a directory whose
        // entries are gone.
        std::string name;
        bool directory;
        if(listed.size() > (levels.empty() ? 0 : levels.back().entries)) {
            auto entry = std::move(listed.back());
            listed.pop_back();
            directory = entry.type == DT_DIR;
            if(entry.type == DT_UNKNOWN) {
                struct stat info;
                if(::fstatat(dir.fd, entry.name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
                    if(names_nothing(errno)) {
                        continue;
                    }
                    return std::unexpected(posix::last_error());
                }
                directory = S_ISDIR(info.st_mode);
            }
            if(directory) {
                Descriptor opened(::openat(dir.fd,
                                           entry.name.c_str(),
                                           O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
                if(opened.fd >= 0) {
                    auto start = listed.size();
                    if(auto error = list_entries(listed, opened.fd)) {
                        return std::unexpected(error);
                    }
                    if(listed.size() > start) {
                        struct stat info;
                        if(::fstat(opened.fd, &info) != 0) {
                            return std::unexpected(posix::last_error());
                        }
                        levels.push_back({.name = std::move(entry.name),
                                          .device = info.st_dev,
                                          .inode = info.st_ino,
                                          .entries = start});
                        dir = std::move(opened);
                        continue;
                    }
                } else if(errno == ENOENT) {
                    continue;
                } else if(errno == ENOTDIR || errno == ELOOP) {
                    // No longer a directory: it goes as a file.
                    directory = false;
                } else {
                    return std::unexpected(posix::last_error());
                }
            }
            name = std::move(entry.name);
        } else {
            if(levels.empty()) {
                return removed;
            }
            // The directory is empty: back to the one above to remove it.
            auto level = std::move(levels.back());
            levels.pop_back();
            if(levels.empty()) {
                dir = Descriptor(AT_FDCWD);
            } else {
                Descriptor above(::openat(dir.fd, "..", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
                struct stat info;
                if(above.fd < 0 || ::fstat(above.fd, &info) != 0) {
                    return std::unexpected(posix::last_error());
                }
                // A rename moved the directory out of the one the walk came
                // from, which it has lost.
                if(info.st_dev != levels.back().device || info.st_ino != levels.back().inode) {
                    return std::unexpected(
                        std::make_error_code(std::errc::no_such_file_or_directory));
                }
                dir = std::move(above);
            }
            name = std::move(level.name);
            directory = true;
        }
        if(::unlinkat(dir.fd, name.c_str(), directory ? AT_REMOVEDIR : 0) == 0) {
            ++removed;
        } else if(errno != ENOENT) {
            return std::unexpected(posix::last_error());
        }
    }
}

#endif

std::error_code create_link(std::string_view target, std::string_view link, bool directory) {
#ifdef _WIN32
    win32::WidePath native;
    if(auto error = windows::to_native(native, link)) {
        return error;
    }
    // The target stays as written, relative or not; only `\` separates in it.
    small_string<256> spelled(target);
    path::to_backslash(spelled);
    win32::WidePath wide;
    if(auto error = win32::widen(wide, spelled)) {
        return error;
    }
    DWORD flags = directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
    // Developer mode lets anyone create a link, but older systems reject the
    // flag that asks for it.
    constexpr DWORD unprivileged = 0x2;
    if(::CreateSymbolicLinkW(native.c_str(), wide.c_str(), flags | unprivileged)) {
        return {};
    }
    if(::GetLastError() == ERROR_INVALID_PARAMETER &&
       ::CreateSymbolicLinkW(native.c_str(), wide.c_str(), flags)) {
        return {};
    }
    return windows::last_error();
#else
    // POSIX tells a link to a directory by its target alone.
    static_cast<void>(directory);
    posix::CPath c_target;
    if(auto error = c_target.assign(target)) {
        return error;
    }
    posix::CPath c_link;
    if(auto error = c_link.assign(link)) {
        return error;
    }
    return ::symlink(c_target.c_str(), c_link.c_str()) == 0 ? std::error_code()
                                                            : posix::last_error();
#endif
}

/// Whether `path` names a root, or by its last name a directory it goes
/// through: `.` and `..`, which the OS refuses to remove only once their
/// contents are gone, or on Windows, which resolves them first, not at all.
/// Win32 also takes the trailing dots and spaces off a last name no
/// separator follows, but in a verbatim path: one made of them alone names
/// the directory it is in.
bool names_root_or_ancestor(std::string_view path) {
    auto name = path::filename(path);
    if(name.empty()) {
        // A root has no name, nor has an empty path, which names nothing.
        return !path.empty();
    }
    if(name == "." || name == "..") {
        return true;
    }
#ifdef _WIN32
    return !path::is_separator(path.back()) && !path.starts_with(R"(\\?\)") &&
           name.find_first_not_of(". ") == std::string_view::npos;
#else
    return false;
#endif
}

}  // namespace

struct detail::OpenDirectory {
#ifdef _WIN32
    win32::Handle handle;

    /// The volume's serial number, which the IDs it lists are on.
    std::uint64_t device;

    Listing listing{.extended = true};

    HANDLE native_handle() const {
        return handle.get();
    }
#else
    DirHandle dir;

    int native_handle() const {
        return ::dirfd(dir.get());
    }
#endif
};

Directory::Entry::Entry() = default;

std::expected<Directory, std::error_code> Directory::open(std::string_view path) {
#ifdef _WIN32
    auto handle = open_directory(path);
    if(!handle) {
        return std::unexpected(handle.error());
    }
    auto id = win32::file_id(handle->get());
    if(!id) {
        return std::unexpected(id.error());
    }
    return Directory(std::make_shared<detail::OpenDirectory>(std::move(*handle), id->device));
#else
    auto opened = open_dir(path);
    if(!opened) {
        return std::unexpected(opened.error());
    }
    return Directory(std::make_shared<detail::OpenDirectory>(std::move(*opened)));
#endif
}

Directory::Iterator Directory::begin() {
    if(!started) {
        started = true;
        advance();
    }
    return Iterator(this);
}

void Directory::advance() {
    if(!current) {
        done = true;
        return;
    }
    if(!directory) {
        current = std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
        return;
    }
    Entry made;
#ifdef _WIN32
    auto entry = directory->listing.next(directory->native_handle());
    if(!entry) {
        current = std::unexpected(windows::to_error_code(entry.error()));
        return;
    }
    if(*entry == nullptr) {
        done = true;
        return;
    }
    if(directory->listing.extended) {
        const auto& info = *static_cast<const FILE_ID_EXTD_DIR_INFO*>(*entry);
        from_utf16(made.name, name_of<FILE_ID_EXTD_DIR_INFO>(*entry));
        made.listed = listed_status(info, info.ReparsePointTag);
        made.listed->id = win32::id_of(directory->device, info.FileId);
    } else {
        // A reparse point's tag stands where the size of its extended
        // attributes would.
        const auto& info = *static_cast<const FILE_FULL_DIR_INFO*>(*entry);
        from_utf16(made.name, name_of<FILE_FULL_DIR_INFO>(*entry));
        made.listed = listed_status(info, info.EaSize);
        made.directory = directory;
    }
    made.type = made.listed->type;
#else
    auto entry = next_entry(directory->dir.get());
    if(!entry) {
        current = std::unexpected(entry.error());
        return;
    }
    if(*entry == nullptr) {
        done = true;
        return;
    }
    made.name = (*entry)->d_name;
    made.type = type_of((*entry)->d_type);
    made.directory = directory;
    struct stat info;
    if(made.type == FileType::Unknown &&
       ::fstatat(directory->native_handle(), made.name.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0) {
        made.type = posix::make_status(info).type;
    }
#endif
    current = std::move(made);
}

std::expected<Volume, std::error_code> Directory::volume() const {
#ifdef _WIN32
    return win32::volume_of(native_handle());
#else
    return posix::volume_of(native_handle());
#endif
}

std::expected<Status, std::error_code> Directory::status() const {
    return detail::handle_status(native_handle());
}

std::error_code Directory::canonical(buffer_ref out) const {
    return detail::handle_canonical(out, native_handle());
}

std::error_code Directory::sync() {
    return detail::handle_sync(native_handle());
}

void Directory::close() {
    directory.reset();
}

Directory::native_handle_type Directory::native_handle() const {
    return directory ? directory->native_handle() : File::closed;
}

std::expected<Status, std::error_code> Directory::Entry::status() const {
#ifdef _WIN32
    // One made rather than listed has no listing to tell it.
    if(!listed) {
        return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
    }
    if(!directory) {
        return *listed;
    }
    // By the name as listed, relative to the directory, which Win32 would
    // rewrite (`a.` naming `a`); back in the UTF-16 it came from, which
    // cannot fail.
    win32::WidePath wide;
    [[maybe_unused]] auto converted = to_utf16(wide, name);
    auto child = win32::open_at(directory->native_handle(), wide, 0, false);
    if(child) {
        return win32::handle_status(child->get());
    }
    // One no open reaches, as status() takes it: what the listing told,
    // without an ID or links.
    if(child.error() == ERROR_SHARING_VIOLATION || child.error() == ERROR_ACCESS_DENIED) {
        return *listed;
    }
    return std::unexpected(windows::to_error_code(child.error()));
#else
    // One made rather than listed has no directory to ask through.
    if(!directory) {
        return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
    }
    struct stat info;
    if(::fstatat(directory->native_handle(), name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
        return std::unexpected(posix::last_error());
    }
    return posix::make_status(info);
#endif
}

Walk::Entry::Entry() = default;

Walk::Walk(std::string_view root, const Options& options) :
    root(root), options(options), reached(Listed{.name = {}, .type = FileType::Directory}) {}

Walk walk(std::string_view root, const Walk::Options& options) {
    return Walk(root, options);
}

Walk::Iterator Walk::begin() {
    if(!started) {
        started = true;
        // The root, which is read first, is no directory to skip.
        skipped = false;
        advance();
    }
    return Iterator(this);
}

void Walk::advance() {
    if(reached && reached->type == FileType::Directory && !skipped &&
       levels.size() < options.max_depth) {
        // The root, or the directory the range stands on.
        std::string path = root;
        if(!levels.empty()) {
            path = levels.back().path;
            path::push(path, reached->name);
        }
        auto start = listed.size();
        auto directory = Directory::open(path);
        std::error_code error = directory ? std::error_code() : directory.error();
        if(directory) {
            for(auto& entry: *directory) {
                if(!entry) {
                    error = entry.error();
                    break;
                }
                listed.push_back({.name = std::move(entry->name), .type = entry->type});
            }
        }
        if(error) {
            listed.resize(start);
            reached.reset();
            current = std::unexpected(Error{.path = std::move(path), .code = error});
            return;
        }
        // Reached from the back, in the order listed.
        std::ranges::reverse(listed.begin() + static_cast<std::ptrdiff_t>(start), listed.end());
        levels.push_back({.path = std::move(path), .listed = start});
    }
    skipped = false;
    while(!levels.empty() && listed.size() == levels.back().listed) {
        levels.pop_back();
    }
    if(levels.empty()) {
        done = true;
        return;
    }
    reached = std::move(listed.back());
    listed.pop_back();
    Entry entry;
    entry.path = levels.back().path;
    path::push(entry.path, reached->name);
    entry.depth = levels.size();
    entry.type = reached->type;
    entry.name_start = entry.path.size() - reached->name.size();
    if(entry.depth == 1) {
        relative_start = entry.name_start;
    }
    entry.relative_start = relative_start;
    current = std::move(entry);
}

std::expected<bool, std::error_code> create_directory(std::string_view path) {
#ifdef _WIN32
    win32::WidePath native;
    if(auto error = windows::to_native(native, path)) {
        return std::unexpected(error);
    }
    if(::CreateDirectoryW(native.c_str(), nullptr)) {
        return true;
    }
    auto error = windows::last_error();
#else
    posix::CPath c_path;
    if(auto error = c_path.assign(path)) {
        return std::unexpected(error);
    }
    if(::mkdir(c_path.c_str(), 0777) == 0) {
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
    if(names_root_or_ancestor(path)) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
#ifdef _WIN32
    win32::WidePath native;
    if(auto error = windows::to_native(native, path)) {
        return std::unexpected(error);
    }
    auto handle = win32::open_native(native, DELETE, false);
    if(!handle) {
        if(names_nothing(handle.error())) {
            return false;
        }
        return std::unexpected(windows::to_error_code(handle.error()));
    }
    if(auto error = dispose(handle->get()); error != ERROR_SUCCESS) {
        return std::unexpected(windows::to_error_code(error));
    }
    return true;
#else
    posix::CPath c_path;
    if(auto error = c_path.assign(posix::without_trailing_separators(path))) {
        return std::unexpected(error);
    }
    if(::unlink(c_path.c_str()) == 0) {
        return true;
    }
    auto error = errno;
    if(names_nothing(error)) {
        return false;
    }
    // A directory: Linux says EISDIR, macOS EPERM, which a file may give for
    // want of permission too.
    if(error == EISDIR || error == EPERM) {
        if(::rmdir(c_path.c_str()) == 0) {
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
    if(names_root_or_ancestor(path)) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
#ifdef _WIN32
    win32::WidePath native;
    if(auto error = windows::to_native(native, path)) {
        return std::unexpected(error);
    }
    auto handle =
        open_to_remove([&](DWORD access) { return win32::open_native(native, access, false); });
    if(!handle) {
        if(names_nothing(handle.error())) {
            return 0;
        }
        return std::unexpected(windows::to_error_code(handle.error()));
    }
    return remove_tree(std::move(*handle));
#else
    posix::CPath c_path;
    if(auto error = c_path.assign(posix::without_trailing_separators(path))) {
        return std::unexpected(error);
    }
    return remove_tree(c_path.c_str());
#endif
}

std::error_code rename(std::string_view from, std::string_view to) {
    if(names_root_or_ancestor(from) || names_root_or_ancestor(to)) {
        return std::make_error_code(std::errc::invalid_argument);
    }
#ifdef _WIN32
    win32::WidePath source;
    if(auto error = windows::to_native(source, from)) {
        return error;
    }
    win32::WidePath target;
    if(auto error = windows::to_native(target, to)) {
        return error;
    }
    auto handle = win32::open_native(source, DELETE, false);
    if(!handle) {
        return windows::to_error_code(handle.error());
    }
    auto name_bytes = target.size() * sizeof(wchar_t);
    std::vector<std::byte> buffer(offsetof(RenameInfoEx, FileName) + name_bytes + sizeof(wchar_t));
    auto* info = reinterpret_cast<RenameInfoEx*>(buffer.data());
    info->Flags = rename_replace | rename_posix_semantics | rename_ignore_readonly;
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(name_bytes);
    std::memcpy(buffer.data() + offsetof(RenameInfoEx, FileName),
                target.c_str(),
                name_bytes + sizeof(wchar_t));
    if(::SetFileInformationByHandle(handle->get(),
                                    rename_info_ex,
                                    info,
                                    static_cast<DWORD>(buffer.size()))) {
        return {};
    }
    if(auto error = ::GetLastError(); !unsupported(error)) {
        return windows::to_error_code(error);
    }
    // A file system without POSIX renames (FAT), which replaces no
    // read-only file: `to` goes without the attribute meanwhile. Should the
    // move fail, the attribute goes back only on the same file, which
    // another may have replaced meanwhile.
    *handle = win32::Handle();
    if(::MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        return {};
    }
    auto error = ::GetLastError();
    auto before = open_attributes(target);
    if(!before || !(before->basic.FileAttributes & FILE_ATTRIBUTE_READONLY) ||
       !win32::set_attributes(before->handle.get(),
                              before->basic.FileAttributes & ~FILE_ATTRIBUTE_READONLY)) {
        return windows::to_error_code(error);
    }
    // The move fails while any handle to `to` is open.
    before->handle = win32::Handle();
    if(::MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        return {};
    }
    error = ::GetLastError();
    if(auto after = open_attributes(target);
       after && after->id == before->id &&
       after->basic.CreationTime.QuadPart == before->basic.CreationTime.QuadPart &&
       after->basic.LastWriteTime.QuadPart == before->basic.LastWriteTime.QuadPart) {
        win32::set_attributes(after->handle.get(), before->basic.FileAttributes);
    }
    return windows::to_error_code(error);
#else
    posix::CPath source;
    if(auto error = source.assign(from)) {
        return error;
    }
    posix::CPath target;
    if(auto error = target.assign(to)) {
        return error;
    }
    return ::rename(source.c_str(), target.c_str()) == 0 ? std::error_code() : posix::last_error();
#endif
}

std::error_code rename_noreplace(std::string_view from, std::string_view to) {
    if(names_root_or_ancestor(from) || names_root_or_ancestor(to)) {
        return std::make_error_code(std::errc::invalid_argument);
    }
#ifdef _WIN32
    win32::WidePath source;
    if(auto error = windows::to_native(source, from)) {
        return error;
    }
    win32::WidePath target;
    if(auto error = windows::to_native(target, to)) {
        return error;
    }
    if(::MoveFileExW(source.c_str(), target.c_str(), 0)) {
        return {};
    }
    auto error = ::GetLastError();
    if(error != ERROR_ACCESS_DENIED) {
        return windows::to_error_code(error);
    }
    // Denied, as is a file deleted while held open: `from`, which is gone
    // but for the name, or on FAT `to`, which keeps the name taken until
    // the last handle goes (NTFS says so itself).
    auto pending = [](std::wstring_view path) {
        auto opened = win32::open_native(path, 0, false);
        return !opened && opened.error() == ERROR_DELETE_PENDING;
    };
    if(pending(source)) {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }
    if(pending(target)) {
        return std::make_error_code(std::errc::file_exists);
    }
    return windows::to_error_code(error);
#else
    posix::CPath source;
    if(auto error = source.assign(from)) {
        return error;
    }
    posix::CPath target;
    if(auto error = target.assign(to)) {
        return error;
    }
    // What the system says when it has no such rename: ENOTSUP on macOS; on
    // Linux EINVAL from a file system without it (NFS), ENOSYS from a kernel
    // without it.
#ifdef __APPLE__
    if(::renamex_np(source.c_str(), target.c_str(), RENAME_EXCL) == 0) {
        return {};
    }
    if(errno != ENOTSUP) {
        return posix::last_error();
    }
#else
    if(::syscall(SYS_renameat2,
                 AT_FDCWD,
                 source.c_str(),
                 AT_FDCWD,
                 target.c_str(),
                 RENAME_NOREPLACE) == 0) {
        return {};
    }
    if(errno != EINVAL && errno != ENOSYS) {
        return posix::last_error();
    }
#endif
    auto refused = posix::last_error();
    // A hard link replaces nothing either. It takes no directory (EPERM),
    // nor, where Linux protects hard links, a file of another owner: the
    // system's refusal says why then.
    if(auto error = create_hard_link(from, to)) {
        return error == std::errc::operation_not_permitted ? refused : error;
    }
    // An old name gone already went to another rename of `from`, which gave
    // the file a new name too. Taking this one back by name could remove a
    // file put there since.
    auto removed = remove(from);
    return removed ? std::error_code() : removed.error();
#endif
}

std::error_code create_symlink(std::string_view target, std::string_view link) {
    return create_link(target, link, false);
}

std::error_code create_directory_symlink(std::string_view target, std::string_view link) {
    return create_link(target, link, true);
}

std::error_code read_symlink(buffer_ref out, std::string_view path) {
#ifdef _WIN32
    win32::WidePath native;
    if(auto error = windows::to_native(native, path)) {
        return error;
    }
    auto handle = win32::open_native(native, 0, false);
    if(!handle) {
        return windows::to_error_code(handle.error());
    }
    // A file that is no reparse point is told by its attributes: FAT, which
    // keeps none, refuses to be asked for a reparse point's data.
    FILE_BASIC_INFO basic;
    if(!::GetFileInformationByHandleEx(handle->get(), FileBasicInfo, &basic, sizeof(basic))) {
        return windows::last_error();
    }
    if(!(basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    // REPARSE_DATA_BUFFER, which only the driver headers declare: a symlink
    // and a junction hold a substitute name, which the system follows, and a
    // print name, which some tools leave empty; a WSL symlink holds its
    // target in UTF-8 after a version.
    struct Reparse {
        ULONG tag;
        USHORT length;
        USHORT reserved;

        struct Names {
            USHORT substitute_offset;
            USHORT substitute_length;
            USHORT print_offset;
            USHORT print_length;
        };

        union {
            struct {
                Names names;
                ULONG flags;
                WCHAR text[1];
            } symlink;

            struct {
                Names names;
                WCHAR text[1];
            } junction;

            struct {
                ULONG version;
                char target[1];
            } wsl;
        };
    };

    constexpr ULONG wsl_symlink_tag = 0xA000001D;
    auto buffer = std::make_unique_for_overwrite<std::byte[]>(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    DWORD returned = 0;
    if(!::DeviceIoControl(handle->get(),
                          FSCTL_GET_REPARSE_POINT,
                          nullptr,
                          0,
                          buffer.get(),
                          MAXIMUM_REPARSE_DATA_BUFFER_SIZE,
                          &returned,
                          nullptr)) {
        return windows::last_error();
    }
    auto& data = *reinterpret_cast<Reparse*>(buffer.get());
    // What the file system gave is checked before it is read: a name past
    // the end of the data is no link to follow.
    auto offset_of = [&](const void* at) {
        return static_cast<std::size_t>(static_cast<const std::byte*>(at) -
                                        reinterpret_cast<const std::byte*>(&data));
    };
    auto read = [&](const Reparse::Names& names, WCHAR* text) {
        auto start = offset_of(text);
        if(start > returned) {
            return std::make_error_code(std::errc::invalid_argument);
        }
        // The print name is what the link was made with; some tools leave
        // it empty, and the substitute name, which the system follows, is
        // all there is.
        bool printed = names.print_length != 0;
        std::size_t offset = printed ? names.print_offset : names.substitute_offset;
        std::size_t length = printed ? names.print_length : names.substitute_length;
        if(offset + length > returned - start) {
            return std::make_error_code(std::errc::invalid_argument);
        }
        std::span name(text + offset / sizeof(WCHAR), length / sizeof(WCHAR));
        std::wstring_view spelled(name.data(), name.size());
        // An absolute substitute name starts with the NT prefix `\??\`,
        // which Win32 spells `\\?\`.
        if(spelled.starts_with(LR"(\??\)")) {
            name[1] = L'\\';
            windows::from_native(out, spelled);
        } else {
            from_utf16(out, spelled);
        }
        return std::error_code();
    };
    switch(data.tag) {
        case IO_REPARSE_TAG_SYMLINK: return read(data.symlink.names, data.symlink.text);
        case IO_REPARSE_TAG_MOUNT_POINT: return read(data.junction.names, data.junction.text);
        case wsl_symlink_tag: {
            auto start = offset_of(data.wsl.target);
            auto end = offset_of(&data.wsl) + data.length;
            if(end < start || end > returned) {
                return std::make_error_code(std::errc::invalid_argument);
            }
            out.assign(std::string_view(data.wsl.target, end - start));
            return {};
        }
        // Another link, whose target only its own driver knows, or a
        // reparse point that holds the file itself, which is no link.
        default:
            return std::make_error_code(win32::is_name_surrogate(data.tag)
                                            ? std::errc::not_supported
                                            : std::errc::invalid_argument);
    }
#else
    posix::CPath c_path;
    if(auto error = c_path.assign(path)) {
        return error;
    }
    // A link's size says nothing in /proc: grow until the target fits.
    auto target = out.resize(256);
    while(true) {
        auto size = ::readlink(c_path.c_str(), target.data(), target.size());
        if(size < 0) {
            out.clear();
            return posix::last_error();
        }
        if(static_cast<std::size_t>(size) < target.size()) {
            out.resize(static_cast<std::size_t>(size));
            return {};
        }
        target = out.resize(target.size() * 2);
    }
#endif
}

std::error_code create_hard_link(std::string_view target, std::string_view link) {
#ifdef _WIN32
    win32::WidePath native_target;
    if(auto error = windows::to_native(native_target, target)) {
        return error;
    }
    win32::WidePath native_link;
    if(auto error = windows::to_native(native_link, link)) {
        return error;
    }
    if(!::CreateHardLinkW(native_link.c_str(), native_target.c_str(), nullptr)) {
        return windows::last_error();
    }
    return {};
#else
    posix::CPath c_target;
    if(auto error = c_target.assign(target)) {
        return error;
    }
    posix::CPath c_link;
    if(auto error = c_link.assign(link)) {
        return error;
    }
    // Not link(), which follows a symlink on macOS.
    if(::linkat(AT_FDCWD, c_target.c_str(), AT_FDCWD, c_link.c_str(), 0) != 0) {
        return posix::last_error();
    }
    return {};
#endif
}

}  // namespace kota::fs
