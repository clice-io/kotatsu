#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "kota/fs/status.h"

namespace kota::fs {

/// How File::open() opens a file. At least one of `read`, `write` and
/// `append` is set; `truncate`, `create` and `create_new` need `write` or
/// `append`, and `truncate` with `append` needs `create_new`, which empties
/// nothing: anything else fails with std::errc::invalid_argument.
struct OpenOptions {
    bool read = false;

    /// For writing, from where the last write or seek left off.
    bool write = false;

    /// For writing, each write at the end of the file, wherever another
    /// handle wrote meanwhile: writing implied.
    bool append = false;

    /// Emptied once open.
    bool truncate = false;

    /// Created when it does not exist.
    bool create = false;

    /// Created, and anything there fails with std::errc::file_exists, a
    /// symlink too, dangling or not; `create` and `truncate` then change
    /// nothing.
    bool create_new = false;
};

/// Where File::seek() counts from.
enum class SeekFrom : std::uint8_t {
    Start,
    Current,
    End,
};

/// An open file, closed when this goes. Its handle is not inherited by
/// child processes, and on Windows does not keep another process from
/// renaming or removing the file.
class File {
public:
#ifdef _WIN32
    using native_handle_type = void*;
#else
    using native_handle_type = int;
#endif

    /// What a closed File holds: CreateFileW never gives a null handle.
#ifdef _WIN32
    static constexpr native_handle_type closed = nullptr;
#else
    static constexpr native_handle_type closed = -1;
#endif

    File() = default;

    File(File&& other) noexcept;

    File& operator=(File&& other) noexcept;

    ~File();

    static std::expected<File, std::error_code> open(std::string_view path,
                                                     const OpenOptions& options = {.read = true});

    /// Reads what fits into `buffer` from where the last read stopped: how
    /// many bytes it read, zero at the end of the file.
    std::expected<std::size_t, std::error_code> read(std::span<char> buffer);

    /// Writes all of `bytes`.
    std::error_code write(std::string_view bytes);

    /// Moves where the next read or write starts to `offset` bytes from
    /// `from`: the new position, from the start. Past the end is no error,
    /// and a write there leaves a hole that reads as zeros; before the start
    /// fails with std::errc::invalid_argument.
    std::expected<std::uint64_t, std::error_code> seek(std::int64_t offset,
                                                       SeekFrom from = SeekFrom::Start);

    /// The status of the open file, which a rename or a removal of its name
    /// does not change, as status() would give it for its path.
    std::expected<Status, std::error_code> status() const;

    /// Cuts the file short at `size` bytes, or extends it there with zeros;
    /// the position stays. Needs `write`.
    std::error_code resize(std::uint64_t size);

    /// Waits until the system has written the file's bytes and status to
    /// the disk, where a crash of the system does not lose them: fsync, on
    /// macOS F_FULLFSYNC (which also empties the disk's own cache, where
    /// fsync stops at the disk), on Windows FlushFileBuffers.
    std::error_code sync();

    /// Lets the file hold holes, ranges never written that take no space:
    /// a write past the end, or resize(), leaves one rather than zeros
    /// written out. On Windows the file system must support it (NTFS and
    /// ReFS do, FAT does not), and the file be open for `write`; POSIX file
    /// systems that can hold holes always do, and there this does nothing.
    std::error_code set_sparse();

    /// Waits until no other handle holds a lock of the file, then takes an
    /// exclusive one, which keeps any other from taking either kind. Locks
    /// are advisory: they keep no one from reading or writing. A handle
    /// holds one lock at a time, and taking one replaces the one it holds;
    /// closing the handle releases it. Each File::open is a handle of its
    /// own, in this process too. flock on POSIX, which a network file system
    /// may not support; on Windows LockFileEx of a byte past any file's end,
    /// so that the file's bytes stay free to read and write.
    std::error_code lock();

    /// The same for a shared lock, which keeps other handles from taking an
    /// exclusive one, not a shared one.
    std::error_code lock_shared();

    /// lock() without waiting: false when another handle holds a lock.
    std::expected<bool, std::error_code> try_lock();

    /// lock_shared() without waiting: false when another handle holds an
    /// exclusive lock.
    std::expected<bool, std::error_code> try_lock_shared();

    /// Releases the lock this handle holds; holding none is no error.
    std::error_code unlock();

    /// Closes the file, saying whether the system kept every write, which
    /// some file systems tell only now; closing a closed file does nothing.
    std::error_code close();

    bool is_open() const;

    /// The file descriptor, or on Windows the HANDLE; this keeps it.
    native_handle_type native_handle() const {
        return handle;
    }

private:
    explicit File(native_handle_type handle) : handle(handle) {}

    native_handle_type handle = closed;
};

/// The bytes of a file. Read, not mapped: on Windows a mapped file keeps an
/// editor from saving over it.
std::expected<std::string, std::error_code> read_file(std::string_view path);

/// Writes `bytes` as the whole of a file, created or emptied first.
std::error_code write_file(std::string_view path, std::string_view bytes);

/// Writes `bytes` as the whole of a file through a sibling renamed over it,
/// so a concurrent reader sees the old bytes or the new, never a part, and
/// concurrent writers never mix. A symlink at `path` is replaced, not
/// written through. It does not wait for the bytes to reach the disk: after
/// a crash of the system, `path` may hold the old bytes, the new, or none.
///
/// On Windows an open of `path` that races the rename may fail, with
/// std::errc::permission_denied or no_such_file_or_directory, while the
/// system swaps the files under the name: a reader that sees either while
/// writers run tries again.
std::error_code write_file_atomic(std::string_view path, std::string_view bytes);

/// A file create_temp_file() made, open for reading and writing.
struct TempFile {
    File file;

    std::string path;
};

/// Creates an empty file in `directory`, named `prefix`, random characters
/// and `suffix`, and none of it there before. Nothing removes it.
std::expected<TempFile, std::error_code> create_temp_file(std::string_view directory,
                                                          std::string_view prefix,
                                                          std::string_view suffix);

/// Creates an empty directory in `directory`, named `prefix` and random
/// characters, and none of it there before: its path. Nothing removes it.
std::expected<std::string, std::error_code> create_temp_directory(std::string_view directory,
                                                                  std::string_view prefix);

}  // namespace kota::fs
