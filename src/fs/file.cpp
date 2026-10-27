#include "kota/fs/file.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <utility>

#include "native.h"
#include "kota/support/small_string.h"
#include "kota/fs/directory.h"
#include "kota/fs/path.h"

#ifdef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <io.h>

#include "win32.h"
#else
#include <fcntl.h>
#include <format>
#include <memory>
#include <sys/file.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/clonefile.h>
#include <sys/param.h>
#else
#include <sys/sendfile.h>
#include <sys/syscall.h>

// copy_file_range's number on x86-64 and on the architectures of the generic
// table, which C libraries before glibc 2.27, and their kernel headers before
// Linux 4.5, leave out.
#ifndef SYS_copy_file_range
#if defined(__x86_64__)
#define SYS_copy_file_range 326
#elif defined(__aarch64__) || defined(__riscv)
#define SYS_copy_file_range 285
#endif
#endif
#endif

#include "posix.h"
#endif

namespace kota::fs {

namespace {

/// Bytes past which one read or write call takes a piece at a time: Windows
/// counts in DWORD, POSIX in ssize_t, and either may stop short anyway.
constexpr std::size_t largest_call = std::size_t{1} << 30;

/// The combinations OpenOptions refuses, as Rust's OpenOptions does.
bool refused(const OpenOptions& options) {
    bool writes = options.write || options.append;
    return (!options.read && !writes) ||
           (!writes && (options.truncate || options.create || options.create_new)) ||
           (options.append && options.truncate && !options.create_new);
}

/// Makes `make(path)` succeed with `path` a new random name in `directory`
/// each time, while what it fails with says the name is taken: another
/// process holds a hundred names only by design.
template <typename Make>
std::error_code with_random_name(buffer_ref path,
                                 std::string_view directory,
                                 std::string_view prefix,
                                 std::string_view suffix,
                                 Make make) {
    // A separator, or on Windows the colon of a drive or a stream, would
    // put the file elsewhere than in `directory`.
    auto elsewhere = [](char c) {
        return path::is_separator(c) || (path::native_style == path::Style::Windows && c == ':');
    };
    if(std::ranges::any_of(prefix, elsewhere) || std::ranges::any_of(suffix, elsewhere)) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    constexpr std::string_view alphabet = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device device;
    std::uniform_int_distribution<std::size_t> pick(0, alphabet.size() - 1);
    // The directory may view `path`, which each name tried rewrites.
    small_string<256> parent(directory);
    small_string<64> name;
    std::error_code error;
    for(int attempt = 0; attempt < 100; ++attempt) {
        name.assign(prefix);
        for(int i = 0; i < 12; ++i) {
            name.push_back(alphabet[pick(device)]);
        }
        name.append(suffix);
        path.assign(parent);
        path::push(path, name);
        error = make(path.view());
        if(error != std::errc::file_exists) {
            break;
        }
    }
    return error;
}

#ifdef _WIN32

/// The bytes from where `handle` stands to the end of the file, zero where
/// it reports no size: a pipe, a console.
std::uint64_t left_to_read(HANDLE handle) {
    LARGE_INTEGER size;
    LARGE_INTEGER position;
    if(!::GetFileSizeEx(handle, &size) ||
       !::SetFilePointerEx(handle, LARGE_INTEGER{}, &position, FILE_CURRENT) ||
       position.QuadPart >= size.QuadPart) {
        return 0;
    }
    return static_cast<std::uint64_t>(size.QuadPart - position.QuadPart);
}

/// The byte a lock covers: past any file's end, so that it keeps no one
/// from the file's bytes, as a Windows lock of them would.
OVERLAPPED lock_byte() {
    OVERLAPPED at{};
    at.Offset = 0xFFFF'FFFE;
    at.OffsetHigh = 0x7FFF'FFFF;
    return at;
}

/// The flags of the locks take_lock() takes.
constexpr DWORD exclusive = LOCKFILE_EXCLUSIVE_LOCK;
constexpr DWORD shared = 0;
constexpr DWORD no_wait = LOCKFILE_FAIL_IMMEDIATELY;

/// Takes a lock with LockFileEx `flags`: false when another handle holds a
/// lock in the way and `flags` say not to wait.
std::expected<bool, std::error_code> take_lock(HANDLE handle, DWORD flags) {
    // A lock replaces the one the handle holds, as flock's does; Windows
    // would have the two conflict.
    auto at = lock_byte();
    ::UnlockFileEx(handle, 0, 1, 0, &at);
    at = lock_byte();
    if(::LockFileEx(handle, flags, 0, 1, 0, &at)) {
        return true;
    }
    auto error = ::GetLastError();
    if(error == ERROR_LOCK_VIOLATION && (flags & LOCKFILE_FAIL_IMMEDIATELY)) {
        return false;
    }
    return std::unexpected(windows::to_error_code(error));
}

#else

/// open(), again while a signal interrupts it.
int open_file(const char* path, int flags, mode_t mode = 0) {
    int fd;
    do {
        fd = ::open(path, flags, mode);
    } while(fd < 0 && errno == EINTR);
    return fd;
}

/// The bytes from where `fd` stands to the end of the file, zero where it
/// reports no size: a pipe, a file under /proc.
std::uint64_t left_to_read(int fd) {
    struct stat info;
    if(::fstat(fd, &info) != 0) {
        return 0;
    }
    auto position = ::lseek(fd, 0, SEEK_CUR);
    if(position < 0 || position >= info.st_size) {
        return 0;
    }
    return static_cast<std::uint64_t>(info.st_size - position);
}

/// The operations of the locks take_lock() takes.
constexpr int exclusive = LOCK_EX;
constexpr int shared = LOCK_SH;
constexpr int no_wait = LOCK_NB;

/// Takes a lock with flock `operation`: false when another handle holds a
/// lock in the way and `operation` says not to wait.
std::expected<bool, std::error_code> take_lock(int fd, int operation) {
    while(::flock(fd, operation) != 0) {
        if(errno == EWOULDBLOCK) {
            return false;
        }
        if(errno != EINTR) {
            return std::unexpected(posix::last_error());
        }
    }
    return true;
}

/// Copies `in` from where it stands to its end into `out` where it stands.
std::error_code copy_bytes(File& in, File& out) {
#ifndef __APPLE__
    // copy_file_range lets the file system share the blocks (Btrfs, XFS) or
    // copy on its server (NFS, SMB). It fails across file systems (but from
    // Linux 5.3 to 5.18), before Linux 4.5, and where a filter of system
    // calls denies it, where sendfile still copies in the kernel. Either
    // stops at the end the file reports, which for one under /proc is its
    // start: what they leave, the loop below reads, at the cost of a read
    // when that is nothing. One that fails leaves the files where it stopped,
    // for the next to go on from, or to fail the same way.
    auto from = in.native_handle();
    auto to = out.native_handle();
    while(::syscall(SYS_copy_file_range, from, nullptr, to, nullptr, largest_call, 0u) > 0) {}
    while(::sendfile(to, from, nullptr, largest_call) > 0) {}
#endif
    constexpr std::size_t size = std::size_t{1} << 17;
    auto buffer = std::make_unique_for_overwrite<char[]>(size);
    while(true) {
        auto count = in.read({buffer.get(), size});
        if(!count) {
            return count.error();
        }
        if(*count == 0) {
            return {};
        }
        if(auto error = out.write({buffer.get(), *count})) {
            return error;
        }
    }
}

#endif

std::error_code error_of(const std::expected<bool, std::error_code>& taken) {
    return taken ? std::error_code() : taken.error();
}

}  // namespace

File::File(File&& other) noexcept :
    handle(std::exchange(other.handle, closed)), locked(std::exchange(other.locked, false)) {}

File& File::operator=(File&& other) noexcept {
    std::swap(handle, other.handle);
    std::swap(locked, other.locked);
    return *this;
}

File::~File() {
    [[maybe_unused]] auto ignored = close();
}

bool File::is_open() const {
    return handle != closed;
}

std::expected<Status, std::error_code> File::status() const {
    return native::status(handle);
}

std::error_code File::canonical(buffer_ref out) const {
    return native::canonical(out, handle);
}

// Every lock tried marks the File for close() to release: one that fails
// may leave held the lock taken before it, as macOS does.

std::error_code File::lock() {
    locked = true;
    return error_of(take_lock(handle, exclusive));
}

std::error_code File::lock_shared() {
    locked = true;
    return error_of(take_lock(handle, shared));
}

std::expected<bool, std::error_code> File::try_lock() {
    locked = true;
    return take_lock(handle, exclusive | no_wait);
}

std::expected<bool, std::error_code> File::try_lock_shared() {
    locked = true;
    return take_lock(handle, shared | no_wait);
}

std::error_code File::write(std::string_view bytes) {
    while(!bytes.empty()) {
        auto count = write_some(bytes);
        if(!count) {
            return count.error();
        }
        bytes.remove_prefix(*count);
    }
    return {};
}

std::error_code File::read_to_end(buffer_ref out) {
    // Files under /proc report no size, and any file may grow while read:
    // the end is where a read returns nothing. One byte past the size the
    // file reports lets that read find the buffer large enough; a size
    // past what is worth taking on its word (/proc/kcore reports 128 TiB)
    // grows as it is read instead.
    constexpr std::uint64_t trusted = std::uint64_t{1} << 30;
    auto left = left_to_read(handle);
    std::size_t at = 0;
    auto buffer =
        out.resize(left == 0 || left > trusted ? 4096 : static_cast<std::size_t>(left) + 1);
    while(true) {
        if(at == buffer.size()) {
            buffer = out.resize(buffer.size() * 2);
        }
        auto count = read(buffer.subspan(at));
        if(!count) {
            out.resize(at);
            return count.error();
        }
        if(*count == 0) {
            break;
        }
        at += *count;
    }
    out.resize(at);
    return {};
}

#ifdef _WIN32

std::expected<File, std::error_code> File::open(std::string_view path, const OpenOptions& options) {
    if(refused(options)) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    win32::WidePath native;
    if(auto error = windows::to_native(native, path)) {
        return std::unexpected(error);
    }
    // Any handle tells its status, which takes the right to read the
    // attributes.
    DWORD access = FILE_READ_ATTRIBUTES | (options.read ? GENERIC_READ : 0);
    // A handle without the right to write the data where it likes writes at
    // the end alone.
    if(options.append) {
        access |= FILE_GENERIC_WRITE & ~FILE_WRITE_DATA;
    } else if(options.write) {
        access |= GENERIC_WRITE;
    }
    DWORD disposition = options.create_new ? CREATE_NEW
                        : options.create   ? OPEN_ALWAYS
                                           : OPEN_EXISTING;
    // A new file takes the place of nothing, a dangling symlink included.
    DWORD flags = options.create_new ? FILE_FLAG_OPEN_REPARSE_POINT : 0;
    HANDLE handle = ::CreateFileW(native.c_str(),
                                  access,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr,
                                  disposition,
                                  FILE_ATTRIBUTE_NORMAL | flags,
                                  nullptr);
    if(handle == INVALID_HANDLE_VALUE) {
        // A directory, which CreateFileW opens only with
        // FILE_FLAG_BACKUP_SEMANTICS, is refused as access is.
        auto error = ::GetLastError();
        if(error == ERROR_ACCESS_DENIED && is_directory(path)) {
            return std::unexpected(std::make_error_code(std::errc::is_a_directory));
        }
        return std::unexpected(windows::to_error_code(error));
    }
    File file(handle);
    // TRUNCATE_EXISTING and CREATE_ALWAYS would drop a hidden or system
    // file's attributes, or refuse it: an existing file is emptied once open
    // instead.
    bool existed = disposition == OPEN_EXISTING || ::GetLastError() == ERROR_ALREADY_EXISTS;
    if(options.truncate && !options.create_new && existed) {
        if(auto error = file.resize(0)) {
            return std::unexpected(error);
        }
    }
    return file;
}

std::expected<std::size_t, std::error_code> File::read(std::span<char> buffer) {
    DWORD count = 0;
    auto size = static_cast<DWORD>(std::min(buffer.size(), largest_call));
    if(!::ReadFile(handle, buffer.data(), size, &count, nullptr)) {
        auto error = ::GetLastError();
        if(error == ERROR_HANDLE_EOF || error == ERROR_BROKEN_PIPE) {
            return 0;
        }
        return std::unexpected(windows::to_error_code(error));
    }
    return count;
}

std::expected<std::size_t, std::error_code> File::write_some(std::string_view bytes) {
    if(bytes.empty()) {
        return 0;
    }
    DWORD count = 0;
    auto size = static_cast<DWORD>(std::min(bytes.size(), largest_call));
    if(!::WriteFile(handle, bytes.data(), size, &count, nullptr)) {
        return std::unexpected(windows::last_error());
    }
    // A full pipe in PIPE_NOWAIT mode takes nothing and calls it success.
    if(count == 0) {
        return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
    }
    return count;
}

std::expected<std::uint64_t, std::error_code> File::seek(std::int64_t offset, SeekFrom from) {
    DWORD method = from == SeekFrom::Start     ? FILE_BEGIN
                   : from == SeekFrom::Current ? FILE_CURRENT
                                               : FILE_END;
    LARGE_INTEGER distance;
    distance.QuadPart = offset;
    LARGE_INTEGER position;
    if(!::SetFilePointerEx(handle, distance, &position, method)) {
        return std::unexpected(windows::last_error());
    }
    return static_cast<std::uint64_t>(position.QuadPart);
}

std::expected<Status, std::error_code> native::status(File::native_handle_type handle) {
    return win32::handle_status(handle);
}

std::error_code native::canonical(buffer_ref out, File::native_handle_type handle) {
    // Windows names a file deleted while open by where it keeps it until the
    // last handle goes, under `\$Extend\$Deleted`, or by its old name. A pipe
    // says it is deleted too.
    FILE_STANDARD_INFO standard;
    if(!::GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard))) {
        return windows::last_error();
    }
    if(standard.DeletePending) {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }
    if(auto error = win32::final_path(out, handle)) {
        // A device, which no path names.
        if(::GetFileType(handle) != FILE_TYPE_DISK) {
            return std::make_error_code(std::errc::no_such_file_or_directory);
        }
        return error;
    }
    return {};
}

std::error_code File::resize(std::uint64_t size) {
    FILE_END_OF_FILE_INFO end;
    end.EndOfFile.QuadPart = static_cast<LONGLONG>(size);
    if(!::SetFileInformationByHandle(handle, FileEndOfFileInfo, &end, sizeof(end))) {
        return windows::last_error();
    }
    return {};
}

std::error_code File::sync() {
    return ::FlushFileBuffers(handle) ? std::error_code() : windows::last_error();
}

std::error_code File::set_sparse() {
    DWORD returned = 0;
    if(!::DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr)) {
        return windows::last_error();
    }
    return {};
}

std::error_code File::unlock() {
    auto at = lock_byte();
    if(!::UnlockFileEx(handle, 0, 1, 0, &at) && ::GetLastError() != ERROR_NOT_LOCKED) {
        return windows::last_error();
    }
    locked = false;
    return {};
}

#else

std::expected<File, std::error_code> File::open(std::string_view path, const OpenOptions& options) {
    if(refused(options)) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    posix::CPath c_path;
    if(auto error = c_path.assign(path)) {
        return std::unexpected(error);
    }
    bool writes = options.write || options.append;
    int flags = O_CLOEXEC | (!writes ? O_RDONLY : options.read ? O_RDWR : O_WRONLY);
    if(options.append) {
        flags |= O_APPEND;
    }
    if(options.create_new) {
        flags |= O_CREAT | O_EXCL;
    } else {
        flags |= (options.create ? O_CREAT : 0) | (options.truncate ? O_TRUNC : 0);
    }
    int fd = open_file(c_path.c_str(), flags, 0666);
    if(fd < 0) {
        // A directory refused to read fails as a file would.
        auto error = posix::last_error();
        if(error == std::errc::permission_denied && is_directory(path)) {
            return std::unexpected(std::make_error_code(std::errc::is_a_directory));
        }
        return std::unexpected(error);
    }
    // A directory fails to open for writing (EISDIR), but opens to read.
    File file(fd);
    struct stat info;
    if(!writes && ::fstat(fd, &info) == 0 && S_ISDIR(info.st_mode)) {
        return std::unexpected(std::make_error_code(std::errc::is_a_directory));
    }
    return file;
}

std::expected<std::size_t, std::error_code> File::read(std::span<char> buffer) {
    while(true) {
        auto count = ::read(handle, buffer.data(), std::min(buffer.size(), largest_call));
        if(count >= 0) {
            return static_cast<std::size_t>(count);
        }
        if(errno != EINTR) {
            return std::unexpected(posix::last_error());
        }
    }
}

std::expected<std::size_t, std::error_code> File::write_some(std::string_view bytes) {
    while(true) {
        auto count = ::write(handle, bytes.data(), std::min(bytes.size(), largest_call));
        if(count >= 0) {
            return static_cast<std::size_t>(count);
        }
        if(errno != EINTR) {
            return std::unexpected(posix::last_error());
        }
    }
}

std::expected<std::uint64_t, std::error_code> File::seek(std::int64_t offset, SeekFrom from) {
    int whence = from == SeekFrom::Start     ? SEEK_SET
                 : from == SeekFrom::Current ? SEEK_CUR
                                             : SEEK_END;
    auto position = ::lseek(handle, static_cast<off_t>(offset), whence);
    if(position < 0) {
        return std::unexpected(posix::last_error());
    }
    return static_cast<std::uint64_t>(position);
}

std::expected<Status, std::error_code> native::status(File::native_handle_type handle) {
    struct stat info;
    if(::fstat(handle, &info) != 0) {
        return std::unexpected(posix::last_error());
    }
    return posix::make_status(info);
}

std::error_code native::canonical(buffer_ref out, File::native_handle_type handle) {
#ifdef __APPLE__
    // macOS keeps the last name of a file whose names all went.
    struct stat opened;
    if(::fstat(handle, &opened) != 0) {
        return posix::last_error();
    }
    if(opened.st_nlink == 0) {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }
    char buffer[MAXPATHLEN];
    if(::fcntl(handle, F_GETPATH, buffer) != 0) {
        return posix::last_error();
    }
    out.assign(buffer);
    return {};
#else
    // /proc would call a closed one's link missing.
    if(handle == File::closed) {
        return std::make_error_code(std::errc::bad_file_descriptor);
    }
    char link[32];
    auto end = std::format_to_n(link, sizeof(link), "/proc/self/fd/{}", handle).out;
    if(auto error = read_symlink(out, std::string_view(link, end))) {
        return error;
    }
    // A pipe or a socket has a description such as pipe:[N] in place of a
    // name.
    if(!out.view().starts_with('/')) {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }
    // Linux keeps the name the file was opened by, marked " (deleted)" once
    // it went, though another hard link may still name the file. A file may
    // be named so too: the name is the file's while it leads to it.
    if(!out.view().ends_with(" (deleted)")) {
        return {};
    }
    posix::CPath c_path;
    struct stat opened;
    struct stat named;
    if(c_path.assign(out.view()) || ::fstat(handle, &opened) != 0 ||
       ::stat(c_path.c_str(), &named) != 0 || named.st_dev != opened.st_dev ||
       named.st_ino != opened.st_ino) {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }
    return {};
#endif
}

std::error_code File::resize(std::uint64_t size) {
    while(::ftruncate(handle, static_cast<off_t>(size)) != 0) {
        if(errno != EINTR) {
            return posix::last_error();
        }
    }
    return {};
}

std::error_code File::sync() {
#ifdef __APPLE__
    // fsync() stands in only where the file takes no F_FULLFSYNC: on a
    // network file system (ENOTSUP), one without the call (ENOTTY), a
    // read-only one (EROFS), for a device (ENODEV), and for a pipe or a
    // socket (EBADF, where fsync() tells ENOTSUP). A file whose volume went
    // away fails with EBADF too, and fsync() then succeeds without writing;
    // after any other error, such as a flush that failed (EIO), it could
    // succeed as well and hide the error.
    if(::fcntl(handle, F_FULLFSYNC) == 0) {
        return {};
    }
    auto error = errno;
    struct stat info;
    bool unsupported = error == ENOTSUP || error == ENOTTY || error == EROFS || error == ENODEV ||
                       (error == EBADF && ::fstat(handle, &info) == 0 &&
                        (S_ISFIFO(info.st_mode) || S_ISSOCK(info.st_mode)));
    if(!unsupported) {
        return std::error_code(error, std::generic_category());
    }
#endif
    return ::fsync(handle) == 0 ? std::error_code() : posix::last_error();
}

std::error_code File::set_sparse() {
    return {};
}

std::error_code File::unlock() {
    auto error = error_of(take_lock(handle, LOCK_UN));
    if(!error) {
        locked = false;
    }
    return error;
}

#endif

std::error_code File::close() {
    if(!is_open()) {
        return {};
    }
    // A copy of the handle, a child process's among others, would keep the
    // lock past the close, and Windows releases a closed handle's lock only
    // some time later.
    if(locked) {
        [[maybe_unused]] auto ignored = unlock();
    }
    auto closing = std::exchange(handle, closed);
    locked = false;
#ifdef _WIN32
    return ::CloseHandle(closing) ? std::error_code() : windows::last_error();
#else
    // The descriptor is gone whatever close() returns, EINTR included: a
    // retry could close one another thread has opened since.
    return ::close(closing) == 0 ? std::error_code() : posix::last_error();
#endif
}

std::expected<int, std::error_code> File::into_descriptor() && {
    if(!is_open()) {
        return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
    }
#ifdef _WIN32
    // The C runtime records that the handle is not inherited, which it
    // checks when it passes its descriptors to a child.
    int fd = ::_open_osfhandle(reinterpret_cast<std::intptr_t>(handle), _O_NOINHERIT);
    if(fd == -1) {
        return std::unexpected(std::error_code(errno, std::generic_category()));
    }
#else
    int fd = handle;
#endif
    handle = closed;
    locked = false;
    return fd;
}

std::error_code read_file(buffer_ref out, std::string_view path) {
    auto file = File::open(path);
    if(!file) {
        return file.error();
    }
    return file->read_to_end(out);
}

std::error_code write_file(std::string_view path, std::string_view bytes) {
    auto file = File::open(path, {.write = true, .truncate = true, .create = true});
    if(!file) {
        return file.error();
    }
    if(auto error = file->write(bytes)) {
        return error;
    }
    return file->close();
}

std::error_code write_file_atomic(std::string_view path, std::string_view bytes) {
    // The sibling's name starts with the file's, cut short at a character's
    // start so that a name near the file system's limit leaves room for the
    // random part.
    auto name = path::filename(path);
    if(name.size() > 32) {
        std::size_t end = 32;
        while(end > 0 && (static_cast<unsigned char>(name[end]) & 0xC0) == 0x80) {
            --end;
        }
        name = name.substr(0, end);
    }
    small_string<64> prefix(name);
    prefix.push_back('.');
    small_string<256> sibling;
    auto file = create_temp_file(sibling, path::parent(path), prefix, ".tmp");
    if(!file) {
        return file.error();
    }
    auto error = file->write(bytes);
    if(auto closed = file->close(); !error) {
        error = closed;
    }
    if(!error) {
        error = rename(sibling, path);
    }
    if(error) {
        [[maybe_unused]] auto removed = remove(sibling);
    }
    return error;
}

std::error_code copy_file(std::string_view from, std::string_view to) {
#ifdef _WIN32
    win32::WidePath source;
    if(auto error = windows::to_native(source, from)) {
        return error;
    }
    win32::WidePath target;
    if(auto error = windows::to_native(target, to)) {
        return error;
    }
    if(::CopyFileExW(source.c_str(), target.c_str(), nullptr, nullptr, nullptr, 0)) {
        return {};
    }
    auto error = windows::last_error();
    // CopyFileExW fails on a directory, and on two names of one file, as on
    // any file it cannot open: these fail as they do elsewhere.
    auto copied = status(from);
    if(!copied) {
        return error;
    }
    if(copied->type != FileType::Regular) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if(is_directory(to)) {
        return std::make_error_code(std::errc::is_a_directory);
    }
    if(equivalent(from, to) == true) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    return error;
#else
    posix::CPath source_path;
    if(auto error = source_path.assign(from)) {
        return error;
    }
    posix::CPath target_path;
    if(auto error = target_path.assign(to)) {
        return error;
    }
    // Here and for `to`, not to wait for a pipe's other end, as an open of
    // one does: a regular file takes no notice of O_NONBLOCK.
    File source(open_file(source_path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
    if(!source.is_open()) {
        return posix::last_error();
    }
    struct stat copied;
    if(::fstat(source.handle, &copied) != 0) {
        return posix::last_error();
    }
    if(!S_ISREG(copied.st_mode)) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    auto permissions = static_cast<mode_t>(copied.st_mode & 0777);
#ifdef __APPLE__
    // A clone shares the blocks until either file changes, and takes the
    // permissions and the times itself. It makes only a new file (EEXIST),
    // on the volume of `from` (EXDEV), where the file system clones (ENOTSUP:
    // APFS does, HFS+ does not).
    if(::fclonefileat(source.handle, AT_FDCWD, target_path.c_str(), 0) == 0) {
        return {};
    }
    if(errno != EEXIST && errno != EXDEV && errno != ENOTSUP) {
        return posix::last_error();
    }
    const auto& mtime = copied.st_mtimespec;
#else
    const auto& mtime = copied.st_mtim;
#endif
    // Another name of `from`'s would lose its bytes once emptied, and a
    // device's permissions are not the copy's to change: what was opened is
    // checked, whatever replaced the name meanwhile.
    auto refused = [&](const struct stat& existing) {
        return !S_ISREG(existing.st_mode) ||
               (existing.st_dev == copied.st_dev && existing.st_ino == copied.st_ino);
    };
    // Created with the permissions it ends with, so that no one opens it
    // meanwhile who could not read `from`.
    File target(
        open_file(target_path.c_str(), O_WRONLY | O_CREAT | O_NONBLOCK | O_CLOEXEC, permissions));
    struct stat existing;
    if(!target.is_open()) {
        auto error = posix::last_error();
        // A pipe without a reader (ENXIO), and another name of a read-only
        // `from`'s (EACCES), fail to open: they fail as they would open. A
        // directory fails as it does (EISDIR).
        if(::stat(target_path.c_str(), &existing) == 0 && !S_ISDIR(existing.st_mode) &&
           refused(existing)) {
            return std::make_error_code(std::errc::invalid_argument);
        }
        return error;
    }
    if(::fstat(target.handle, &existing) != 0) {
        return posix::last_error();
    }
    if(refused(existing)) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    // Emptied before its permissions change, so that no one they let in
    // reads what it held.
    if(auto error = target.resize(0)) {
        return error;
    }
    if(::fchmod(target.handle, permissions) != 0) {
        return posix::last_error();
    }
    if(auto error = copy_bytes(source, target)) {
        return error;
    }
    // The writes moved the mtime, which goes back to `from`'s.
    timespec times[2] = {
        {.tv_sec = 0, .tv_nsec = UTIME_OMIT},
        mtime
    };
    if(::futimens(target.handle, times) != 0) {
        return posix::last_error();
    }
    return target.close();
#endif
}

std::expected<File, std::error_code> create_temp_file(buffer_ref path,
                                                      std::string_view directory,
                                                      std::string_view prefix,
                                                      std::string_view suffix) {
    File file;
    auto error = with_random_name(path, directory, prefix, suffix, [&](std::string_view candidate) {
        auto opened = File::open(candidate, {.read = true, .write = true, .create_new = true});
        if(!opened) {
            return opened.error();
        }
        file = std::move(*opened);
        return std::error_code();
    });
    if(error) {
        return std::unexpected(error);
    }
    return file;
}

std::error_code create_temp_directory(buffer_ref path,
                                      std::string_view directory,
                                      std::string_view prefix) {
    return with_random_name(path, directory, prefix, "", [](std::string_view candidate) {
        auto made = create_directory(candidate);
        if(!made) {
            return made.error();
        }
        // A directory there already holds the name too.
        return *made ? std::error_code() : std::make_error_code(std::errc::file_exists);
    });
}

}  // namespace kota::fs
