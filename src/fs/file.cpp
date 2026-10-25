#include "kota/fs/file.h"

#include <algorithm>
#include <random>
#include <utility>

#include "kota/support/small_string.h"
#include "kota/fs/directory.h"
#include "kota/fs/path.h"

#ifdef _WIN32
#include "win32.h"
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

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

#endif

std::error_code error_of(const std::expected<bool, std::error_code>& taken) {
    return taken ? std::error_code() : taken.error();
}

}  // namespace

File::File(File&& other) noexcept : handle(std::exchange(other.handle, closed)) {}

File& File::operator=(File&& other) noexcept {
    std::swap(handle, other.handle);
    return *this;
}

File::~File() {
    [[maybe_unused]] auto ignored = close();
}

bool File::is_open() const {
    return handle != closed;
}

std::error_code File::lock() {
    return error_of(take_lock(handle, exclusive));
}

std::error_code File::lock_shared() {
    return error_of(take_lock(handle, shared));
}

std::expected<bool, std::error_code> File::try_lock() {
    return take_lock(handle, exclusive | no_wait);
}

std::expected<bool, std::error_code> File::try_lock_shared() {
    return take_lock(handle, shared | no_wait);
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
        return std::unexpected(windows::last_error());
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

std::error_code File::write(std::string_view bytes) {
    while(!bytes.empty()) {
        DWORD count = 0;
        auto size = static_cast<DWORD>(std::min(bytes.size(), largest_call));
        if(!::WriteFile(handle, bytes.data(), size, &count, nullptr)) {
            return windows::last_error();
        }
        bytes.remove_prefix(count);
    }
    return {};
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

std::expected<Status, std::error_code> File::status() const {
    return win32::handle_status(handle);
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
    return {};
}

std::error_code File::close() {
    if(!is_open()) {
        return {};
    }
    auto closing = std::exchange(handle, closed);
    return ::CloseHandle(closing) ? std::error_code() : windows::last_error();
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
    int fd;
    do {
        fd = ::open(c_path.c_str(), flags, 0666);
    } while(fd < 0 && errno == EINTR);
    if(fd < 0) {
        return std::unexpected(posix::last_error());
    }
    return File(fd);
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

std::error_code File::write(std::string_view bytes) {
    while(!bytes.empty()) {
        auto count = ::write(handle, bytes.data(), std::min(bytes.size(), largest_call));
        if(count < 0) {
            if(errno == EINTR) {
                continue;
            }
            return posix::last_error();
        }
        bytes.remove_prefix(static_cast<std::size_t>(count));
    }
    return {};
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

std::expected<Status, std::error_code> File::status() const {
    struct stat info;
    if(::fstat(handle, &info) != 0) {
        return std::unexpected(posix::last_error());
    }
    return posix::make_status(info);
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
    // Network file systems, among others, have no F_FULLFSYNC.
    if(::fcntl(handle, F_FULLFSYNC) == 0) {
        return {};
    }
#endif
    return ::fsync(handle) == 0 ? std::error_code() : posix::last_error();
}

std::error_code File::set_sparse() {
    return {};
}

std::error_code File::unlock() {
    return error_of(take_lock(handle, LOCK_UN));
}

std::error_code File::close() {
    if(!is_open()) {
        return {};
    }
    // The descriptor is gone whatever close() returns, EINTR included: a
    // retry could close one another thread has opened since.
    return ::close(std::exchange(handle, closed)) == 0 ? std::error_code() : posix::last_error();
}

#endif

std::error_code read_file(buffer_ref out, std::string_view path) {
    auto file = File::open(path);
    if(!file) {
        return file.error();
    }
    // Files under /proc report no size, and any file may grow while read:
    // the end is where a read returns nothing.
    std::size_t at = 0;
    auto buffer = out.resize(4096);
    while(true) {
        if(at == buffer.size()) {
            buffer = out.resize(buffer.size() * 2);
        }
        auto count = file->read(buffer.subspan(at));
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
