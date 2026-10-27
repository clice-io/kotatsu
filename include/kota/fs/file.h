#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <system_error>

#include "kota/support/buffer_ref.h"
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

    /// Writes into `out` the bytes from where the file stands to its end:
    /// into a buffer sized once from what the file reports it holds, read on
    /// past that until a read returns nothing, so that a file that grows
    /// meanwhile, or one that reports no size (a pipe, a file under /proc),
    /// is read whole.
    std::error_code read_to_end(buffer_ref out);

    /// Writes from the start of `bytes` what one call of the system takes,
    /// where the last write or seek left off: how many bytes it wrote, which
    /// a pipe or a terminal may make fewer than `bytes` holds. A pipe that
    /// cannot take a byte without waiting, set not to wait (O_NONBLOCK, on
    /// Windows PIPE_NOWAIT), fails with
    /// std::errc::resource_unavailable_try_again.
    std::expected<std::size_t, std::error_code> write_some(std::string_view bytes);

    /// Writes all of `bytes`, write_some() after write_some(). What it wrote
    /// before a failure stays written, and write_some() is the way to count
    /// it.
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

    /// Writes into `out` the name the OS gives the open file, as canonical()
    /// gives it for a path: absolute, every symlink followed, on Windows
    /// with each name's on-disk case; after a rename, the new name. Once the
    /// name it was opened by is gone, the OS may give another hard link of
    /// it; one that knows no name leading to the file fails with
    /// std::errc::no_such_file_or_directory, as Linux, which remembers only
    /// the name gone, always does then. GetFinalPathNameByHandleW on Windows,
    /// F_GETPATH on macOS, and on Linux the link in /proc/self/fd, which
    /// needs /proc mounted.
    std::error_code canonical(buffer_ref out) const;

    /// Cuts the file short at `size` bytes, or extends it there with zeros;
    /// the position stays. Needs `write`: on Windows `append` does not
    /// give the right to change the file's size.
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
    /// closing the File releases it, even where a copy of the handle stays
    /// open, as a child process may hold one. Each File::open is a handle of its
    /// own, in this process too. flock on POSIX, which a network file system
    /// may not support; on Windows LockFileEx of a byte past any file's end,
    /// so that the file's bytes stay free to read and write, and which takes
    /// a file open for `read` or `write`: one open to `append` alone, open it
    /// for `read` too.
    std::error_code lock();

    /// The same for a shared lock, which keeps other handles from taking an
    /// exclusive one, not a shared one.
    std::error_code lock_shared();

    /// lock() without waiting: false when another handle holds a lock. The
    /// lock this handle held may be gone then on Linux and Windows, which
    /// release it before they try, and stays on macOS.
    std::expected<bool, std::error_code> try_lock();

    /// lock_shared() without waiting: false when another handle holds an
    /// exclusive lock, and as try_lock() the lock this handle held may be
    /// gone then.
    std::expected<bool, std::error_code> try_lock_shared();

    /// Releases the lock this handle holds; holding none is no error.
    std::error_code unlock();

    /// Releases the lock this File holds and closes the file, saying whether
    /// the system kept every write, which some file systems tell only now;
    /// closing a closed file does nothing. Windows would release the lock of
    /// a closed handle only "some time later", and not while a copy of the
    /// handle stays open: taking it again at once could fail.
    std::error_code close();

    /// Gives up the handle as a file descriptor of the C runtime, which
    /// `_write`, `_read` and `_close` take on Windows (`_open_osfhandle`,
    /// binary mode), and on POSIX the descriptor itself: the File is closed
    /// then, without releasing the lock it holds, and the descriptor the
    /// caller's to close. On Windows it belongs to the C runtime kotatsu is
    /// linked with, which with a static runtime is each module's own. On
    /// failure the File keeps the handle.
    std::expected<int, std::error_code> into_descriptor() &&;

    bool is_open() const;

    /// The file descriptor, or on Windows the HANDLE; this keeps it.
    native_handle_type native_handle() const {
        return handle;
    }

private:
    friend std::error_code copy_file(std::string_view from, std::string_view to);

    explicit File(native_handle_type handle) : handle(handle) {}

    native_handle_type handle = closed;

    /// Whether this File has taken a lock it may still hold, which close()
    /// then releases.
    bool locked = false;
};

/// Writes into `out` the bytes of a file, as File::read_to_end() reads
/// them. Read, not mapped: on Windows a mapped file keeps an editor from
/// saving over it.
std::error_code read_file(buffer_ref out, std::string_view path);

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

/// Copies the bytes of the regular file `from`, symlinks followed, as the
/// whole of `to`, which it creates, or empties first, through a symlink there
/// too. `to` takes `from`'s mtime and permissions: on POSIX the read, write
/// and run bits (0777), not setuid and setgid, which would run the copy with
/// the rights of whoever copied it; on Windows the attributes, read-only and
/// hidden among them. The system's own copy does it where there is one, which
/// may share the blocks or copy on a share's server: CopyFileExW on Windows,
/// on macOS a clone where `to` is new on `from`'s APFS volume, on Linux
/// copy_file_range, then sendfile across file systems. What else a file
/// carries goes along only as those take it: Windows's copy takes the
/// alternate streams and extended attributes, a macOS clone the extended
/// attributes and the flags (a locked file's clone is locked).
///
/// `to` is written in place: a reader meanwhile may see a part, and after a
/// failure it may hold a part, or nothing. For all or nothing, copy to a
/// sibling and rename() it over, or rename_noreplace() it to keep a file
/// there.
///
/// A `from` that is no regular file, a directory or a pipe among others, and
/// a `to` that is another name of `from`'s, which emptying would lose, or on
/// POSIX a pipe or a device, fail with std::errc::invalid_argument; a
/// directory at `to` with is_a_directory. A read-only `to` fails with
/// permission_denied, but for root, and on Windows a hidden one too; on POSIX
/// a `to` of another owner, whose permissions only its owner sets, with
/// operation_not_permitted.
std::error_code copy_file(std::string_view from, std::string_view to);

/// Creates an empty file in `directory`, named `prefix`, random characters
/// and `suffix`, and none of it there before: the file, open for reading and
/// writing, and its path written into `path`. Nothing removes it. A `prefix`
/// or `suffix` with a separator in it, or on Windows a `:`, which would
/// name a file elsewhere, fails with std::errc::invalid_argument.
std::expected<File, std::error_code> create_temp_file(buffer_ref path,
                                                      std::string_view directory,
                                                      std::string_view prefix,
                                                      std::string_view suffix);

/// Creates an empty directory in `directory`, named `prefix` and random
/// characters, and none of it there before, and writes its path into `path`.
/// Nothing removes it. `prefix` is refused as create_temp_file() refuses
/// it.
std::error_code create_temp_directory(buffer_ref path,
                                      std::string_view directory,
                                      std::string_view prefix);

}  // namespace kota::fs
