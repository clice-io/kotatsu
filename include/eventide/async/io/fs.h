#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "eventide/async/runtime/task.h"
#include "eventide/async/vocab/error.h"
#include "eventide/async/vocab/owned.h"

namespace eventide {

class event_loop;

namespace fs {

using file_time = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>;

struct file_stats {
    /// Device ID containing the file.
    std::uint64_t dev = 0;

    /// File type and mode (permissions).
    std::uint64_t mode = 0;

    /// Number of hard links.
    std::uint64_t nlink = 0;

    /// User ID of owner.
    std::uint64_t uid = 0;

    /// Group ID of owner.
    std::uint64_t gid = 0;

    /// Device ID (if special file).
    std::uint64_t rdev = 0;

    /// Inode number.
    std::uint64_t ino = 0;

    /// Total size in bytes.
    std::uint64_t size = 0;

    /// Preferred I/O block size.
    std::uint64_t blksize = 0;

    /// Number of 512-byte blocks allocated.
    std::uint64_t blocks = 0;

    /// File flags (BSD-specific).
    std::uint64_t flags = 0;

    /// File generation number (BSD-specific).
    std::uint64_t gen = 0;

    /// Last access time.
    file_time atime;

    /// Last modification time.
    file_time mtime;

    /// Last status change time.
    file_time ctime;

    /// Creation (birth) time.
    file_time birthtime;
};

struct mkstemp_result {
    int fd = -1;
    std::string path;
};

struct dirent {
    enum class type {
        unknown,      // type not known
        file,         // regular file
        dir,          // directory
        link,         // symlink
        fifo,         // FIFO/pipe
        socket,       // socket
        char_device,  // character device
        block_device  // block device
    };
    std::string name;
    type kind = type::unknown;
};

struct copyfile_options {
    /// Fail if destination exists.
    bool excl = false;

    /// Try to clone via copy-on-write if supported.
    bool clone = false;

    /// Force clone (may fall back to copy on failure).
    bool clone_force = false;
};

class dir_handle {
public:
    dir_handle() = default;
    dir_handle(dir_handle&& other) noexcept;
    dir_handle& operator=(dir_handle&& other) noexcept;

    dir_handle(const dir_handle&) = delete;
    dir_handle& operator=(const dir_handle&) = delete;

    bool valid() const noexcept;
    void* native_handle() const noexcept;
    void reset() noexcept;

    static dir_handle from_native(void* ptr);

private:
    explicit dir_handle(void* ptr);

    void* dir = nullptr;
};

/// Remove a file.
task<void, error> unlink(std::string_view path, event_loop& loop = event_loop::current());

/// Create a directory with the given permissions.
task<void, error> mkdir(std::string_view path, int mode, event_loop& loop = event_loop::current());

/// Get file status (metadata) by path.
task<file_stats, error> stat(std::string_view path, event_loop& loop = event_loop::current());

/// Copy a file from path to new_path.
task<void, error> copyfile(std::string_view path,
                           std::string_view new_path,
                           copyfile_options options = copyfile_options{},
                           event_loop& loop = event_loop::current());

/// Create a unique temporary directory from a template (must end with "XXXXXX").
task<std::string, error> mkdtemp(std::string_view tpl, event_loop& loop = event_loop::current());

/// Create a unique temporary file from a template (must end with "XXXXXX").
task<mkstemp_result, error> mkstemp(std::string_view tpl, event_loop& loop = event_loop::current());

/// Remove an empty directory.
task<void, error> rmdir(std::string_view path, event_loop& loop = event_loop::current());

/// Scan a directory, returning all entries at once.
task<std::vector<dirent>, error> scandir(std::string_view path,
                                         event_loop& loop = event_loop::current());

/// Open a directory for iterative reading.
task<dir_handle, error> opendir(std::string_view path, event_loop& loop = event_loop::current());

/// Read a batch of entries from an opened directory.
task<std::vector<dirent>, error> readdir(dir_handle& dir, event_loop& loop = event_loop::current());

/// Close an opened directory handle.
task<void, error> closedir(dir_handle& dir, event_loop& loop = event_loop::current());

/// Get file status by file descriptor.
task<file_stats, error> fstat(int fd, event_loop& loop = event_loop::current());

/// Get file status by path, without following symlinks.
task<file_stats, error> lstat(std::string_view path, event_loop& loop = event_loop::current());

/// Rename (move) a file or directory.
task<void, error> rename(std::string_view path,
                         std::string_view new_path,
                         event_loop& loop = event_loop::current());

/// Flush file data and metadata to disk.
task<void, error> fsync(int fd, event_loop& loop = event_loop::current());

/// Flush file data to disk (metadata may not be flushed).
task<void, error> fdatasync(int fd, event_loop& loop = event_loop::current());

/// Truncate a file to the specified length.
task<void, error> ftruncate(int fd, std::int64_t offset, event_loop& loop = event_loop::current());

/// Zero-copy transfer data between file descriptors.
task<std::int64_t, error> sendfile(int out_fd,
                                   int in_fd,
                                   std::int64_t in_offset,
                                   std::size_t length,
                                   event_loop& loop = event_loop::current());

/// Check user permissions for a file (mode: F_OK, R_OK, W_OK, X_OK).
task<void, error> access(std::string_view path, int mode, event_loop& loop = event_loop::current());

/// Change file permissions by path.
task<void, error> chmod(std::string_view path, int mode, event_loop& loop = event_loop::current());

/// Change file access and modification times by path.
task<void, error> utime(std::string_view path,
                        double atime,
                        double mtime,
                        event_loop& loop = event_loop::current());

/// Change file access and modification times by file descriptor.
task<void, error>
    futime(int fd, double atime, double mtime, event_loop& loop = event_loop::current());

/// Change file access and modification times by path, without following symlinks.
task<void, error> lutime(std::string_view path,
                         double atime,
                         double mtime,
                         event_loop& loop = event_loop::current());

/// Create a hard link.
task<void, error> link(std::string_view path,
                       std::string_view new_path,
                       event_loop& loop = event_loop::current());

/// Create a symbolic link.
task<void, error> symlink(std::string_view path,
                          std::string_view new_path,
                          int flags = 0,
                          event_loop& loop = event_loop::current());

/// Read the target of a symbolic link.
task<std::string, error> readlink(std::string_view path, event_loop& loop = event_loop::current());

/// Resolve a path to its canonical absolute pathname.
task<std::string, error> realpath(std::string_view path, event_loop& loop = event_loop::current());

/// Change file permissions by file descriptor.
task<void, error> fchmod(int fd, int mode, event_loop& loop = event_loop::current());

/// Change file owner and group by path.
task<void, error> chown(std::string_view path,
                        std::uint32_t uid,
                        std::uint32_t gid,
                        event_loop& loop = event_loop::current());

/// Change file owner and group by file descriptor.
task<void, error>
    fchown(int fd, std::uint32_t uid, std::uint32_t gid, event_loop& loop = event_loop::current());

/// Change file owner and group by path, without following symlinks.
task<void, error> lchown(std::string_view path,
                         std::uint32_t uid,
                         std::uint32_t gid,
                         event_loop& loop = event_loop::current());

struct fs_stats {
    /// Filesystem type identifier.
    std::uint64_t type = 0;

    /// Fundamental block size in bytes.
    std::uint64_t bsize = 0;

    /// Total number of blocks on the filesystem.
    std::uint64_t blocks = 0;

    /// Number of free blocks.
    std::uint64_t bfree = 0;

    /// Number of free blocks available to unprivileged users.
    std::uint64_t bavail = 0;

    /// Total number of file nodes (inodes).
    std::uint64_t files = 0;

    /// Number of free file nodes.
    std::uint64_t ffree = 0;

    /// Fragment size in bytes.
    std::uint64_t frsize = 0;
};

/// Get filesystem statistics (total/free space, inode counts, etc.).
task<fs_stats, error> statfs(std::string_view path, event_loop& loop = event_loop::current());

/// Open a file asynchronously. Returns the file descriptor on success.
task<int, error>
    open(std::string_view path, int flags, int mode = 0, event_loop& loop = event_loop::current());

/// Read from a file descriptor into a buffer. offset = -1 uses current position.
task<std::size_t, error> read(int fd,
                              std::span<char> buf,
                              std::int64_t offset = -1,
                              event_loop& loop = event_loop::current());

/// Write a buffer to a file descriptor. offset = -1 uses current position.
task<std::size_t, error> write(int fd,
                               std::span<const char> buf,
                               std::int64_t offset = -1,
                               event_loop& loop = event_loop::current());

/// Close a file descriptor asynchronously.
task<void, error> close(int fd, event_loop& loop = event_loop::current());

namespace sync {

/// Open a file. Returns the fd on success.
/// flags: UV_FS_O_RDONLY, UV_FS_O_WRONLY, UV_FS_O_RDWR, etc.
result<int> open(std::string_view path, int flags, int mode = 0);

/// Read up to buf.size() bytes from fd at offset (-1 = current position).
result<std::size_t> read(int fd, std::span<char> buf, std::int64_t offset = -1);

/// Write buf to fd at offset (-1 = current position).
result<std::size_t> write(int fd, std::span<const char> buf, std::int64_t offset = -1);

/// Close a file descriptor.
error close(int fd);

/// Convenience: read entire file into a string.
result<std::string> read_to_string(std::string_view path);

}  // namespace sync

}  // namespace fs

}  // namespace eventide
