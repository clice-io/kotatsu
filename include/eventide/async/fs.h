#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "error.h"
#include "owned.h"
#include "task.h"

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

task<void, error> unlink(std::string_view path, event_loop& loop = event_loop::current());

task<void, error> mkdir(std::string_view path, int mode, event_loop& loop = event_loop::current());

task<file_stats, error> stat(std::string_view path, event_loop& loop = event_loop::current());

task<void, error> copyfile(std::string_view path,
                     std::string_view new_path,
                     copyfile_options options = copyfile_options{},
                     event_loop& loop = event_loop::current());

task<std::string, error> mkdtemp(std::string_view tpl, event_loop& loop = event_loop::current());

task<mkstemp_result, error> mkstemp(std::string_view tpl,
                                    event_loop& loop = event_loop::current());

task<void, error> rmdir(std::string_view path, event_loop& loop = event_loop::current());

task<std::vector<dirent>, error> scandir(std::string_view path,
                                         event_loop& loop = event_loop::current());

task<dir_handle, error> opendir(std::string_view path, event_loop& loop = event_loop::current());

task<std::vector<dirent>, error> readdir(dir_handle& dir, event_loop& loop = event_loop::current());

task<void, error> closedir(dir_handle& dir, event_loop& loop = event_loop::current());

task<file_stats, error> fstat(int fd, event_loop& loop = event_loop::current());

task<file_stats, error> lstat(std::string_view path, event_loop& loop = event_loop::current());

task<void, error> rename(std::string_view path,
                   std::string_view new_path,
                   event_loop& loop = event_loop::current());

task<void, error> fsync(int fd, event_loop& loop = event_loop::current());

task<void, error> fdatasync(int fd, event_loop& loop = event_loop::current());

task<void, error> ftruncate(int fd, std::int64_t offset, event_loop& loop = event_loop::current());

task<std::int64_t, error> sendfile(int out_fd,
                                   int in_fd,
                                   std::int64_t in_offset,
                                   std::size_t length,
                                   event_loop& loop = event_loop::current());

task<void, error> access(std::string_view path, int mode, event_loop& loop = event_loop::current());

task<void, error> chmod(std::string_view path, int mode, event_loop& loop = event_loop::current());

task<void, error> utime(std::string_view path,
                  double atime,
                  double mtime,
                  event_loop& loop = event_loop::current());

task<void, error> futime(int fd, double atime, double mtime, event_loop& loop = event_loop::current());

task<void, error> lutime(std::string_view path,
                   double atime,
                   double mtime,
                   event_loop& loop = event_loop::current());

task<void, error> link(std::string_view path,
                 std::string_view new_path,
                 event_loop& loop = event_loop::current());

}  // namespace fs

class fs_event {
public:
    fs_event() noexcept;

    fs_event(const fs_event&) = delete;
    fs_event& operator=(const fs_event&) = delete;

    fs_event(fs_event&& other) noexcept;
    fs_event& operator=(fs_event&& other) noexcept;

    ~fs_event();

    struct Self;
    Self* operator->() noexcept;

    struct watch_options {
        /// Report creation/removal events (if supported by backend).
        bool watch_entry;

        /// Use stat polling where available.
        bool stat;

        /// Recurse into subdirectories when supported.
        bool recursive;

        constexpr watch_options(bool watch_entry = false,
                                bool stat = false,
                                bool recursive = false) :
            watch_entry(watch_entry), stat(stat), recursive(recursive) {}
    };

    struct change_flags {
        /// Entry renamed or moved.
        bool rename;

        /// Entry content/metadata changed.
        bool change;

        constexpr change_flags(bool rename = false, bool change = false) :
            rename(rename), change(change) {}
    };

    struct change {
        std::string path;
        change_flags flags = {};
    };

    static result<fs_event> create(event_loop& loop = event_loop::current());

    /// Start watching the given path; flags mapped to libuv equivalents.
    error start(const char* path, watch_options options = watch_options{});

    error stop();

    /// Await a change event; delivers one pending change at a time.
    task<change, error> wait();

private:
    explicit fs_event(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

}  // namespace eventide
