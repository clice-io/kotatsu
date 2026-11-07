#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <system_error>

#include "kotatsu/support/buffer_ref.h"
#include "kotatsu/fs/status.h"

namespace kotatsu::fs {

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

/// An open file to read or write bytes, closed when this goes but for a
/// handle it borrows (borrow()): a regular file, a device or a pipe, never a
/// directory, which a Directory opens. A handle it opens is not inherited by
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

    /// Opens the file at `path` as `options` say: a directory fails with
    /// std::errc::is_a_directory, or for `create_new` with file_exists, as
    /// anything there does.
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
    /// fsync stops at the disk), on Windows FlushFileBuffers. That takes a
    /// handle that may write, which a File open to read alone opens the
    /// file again for: a read-only file, or one the caller may not write,
    /// fails there with std::errc::permission_denied.
    std::error_code sync();

    /// Lets the file hold holes, ranges never written that take no space:
    /// a write past the end, or resize(), leaves one rather than zeros
    /// written out. On Windows the file system must support it (NTFS and
    /// ReFS do, FAT does not), and the file be open for `write`; POSIX file
    /// systems that can hold holes always do, and there this does nothing.
    std::error_code set_sparse();

#ifdef _WIN32
    /// Whether the file is marked sparse (FILE_ATTRIBUTE_SPARSE_FILE), as
    /// set_sparse() or another program marks it. Windows alone: POSIX has no
    /// such mark, and whether a file there holds holes is its file system's
    /// affair.
    std::expected<bool, std::error_code> is_sparse() const;
#endif

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

    /// Whether a byte-range lock, a read lock or a write lock, covers any of
    /// the `length` bytes from `offset`, `length` not zero: (0, UINT64_MAX)
    /// asks of every byte. Whose locks count differs: on POSIX other
    /// processes', on Windows every handle's.
    ///
    /// On POSIX fcntl(F_GETLK), which sees the record locks that other
    /// processes take with fcntl or lockf, as SQLite and LMDB do, and never
    /// this process's own: a record lock belongs to the process, not to a
    /// handle. Closing any descriptor of a file drops every record lock the
    /// process holds of it, the File's close among them: probe a file this
    /// process does not otherwise hold open, as LMDB's lock file. No lock
    /// reaches past 2^63 - 1, off_t's largest offset, and an `offset` past
    /// it fails with std::errc::invalid_argument. lock() takes locks of
    /// another family, flock's, which Linux keeps apart and this does not
    /// see; macOS keeps the two families together, and sees a lock lock()
    /// took, of any handle, this process's too, as one of every byte.
    ///
    /// On Windows LockFileEx of the range, without waiting, and UnlockFileEx
    /// when it took it: a lock of any handle counts, this File's and this
    /// process's other handles' included, and for an instant the probe holds
    /// the range itself, which keeps other handles from locking it, and from
    /// reading or writing its bytes. A lock reaches 2^64 - 1 at most, the
    /// last byte, where a range that would pass it ends. lock() locks a byte
    /// past any file's end, which a range that reaches it sees. It takes a
    /// file open for `read` or `write`, as lock() does.
    std::expected<bool, std::error_code> range_locked(std::uint64_t offset,
                                                      std::uint64_t length) const;

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
    /// failure the File keeps the handle. A File that borrows its handle
    /// (borrow()) has none of its own to give: it must not call this.
    std::expected<int, std::error_code> into_descriptor() &&;

    bool is_open() const;

    /// The file descriptor, or on Windows the HANDLE; this keeps it.
    native_handle_type native_handle() const {
        return handle;
    }

    /// A File that works through `handle`, which stays its owner's: every
    /// operation goes through it, and closing the File, or its going,
    /// releases the lock the File took and leaves `handle` open, close()
    /// then telling no error of its own.
    static File borrow(native_handle_type handle);

private:
    friend std::error_code copy_file(std::string_view from, std::string_view to);

    friend std::expected<File, std::error_code> create_temp_file(buffer_ref path,
                                                                 std::string_view directory,
                                                                 std::string_view prefix,
                                                                 std::string_view suffix);

    explicit File(native_handle_type handle) : handle(handle) {}

    native_handle_type handle = closed;

    /// Whether this File has taken a lock it may still hold, which close()
    /// then releases.
    bool locked = false;

    /// Whether close() closes the handle, which it leaves open where the
    /// File borrows it.
    bool owned = true;
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
/// written through. On POSIX a file there lends the new one its permissions
/// (0777: not setuid, setgid and sticky), and its owner and group where the
/// caller may set them, as root always may, or else its group alone where
/// the caller is in it; on Windows its read-only, hidden and system
/// attributes. A new file takes the permissions write_file() gives one.
///
/// A crash of the process leaves `path` with the old bytes or the new, and
/// may leave the sibling behind. It does not wait for the bytes to reach
/// the disk: after a crash of the system, `path` may hold the old bytes,
/// the new, a part of the new or none at all, or, where it was new, be
/// gone.
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
/// A `from` that is no regular file, a directory, a pipe or a device among
/// others, and a `to` that is another name of `from`'s, which emptying would
/// lose, or on POSIX a pipe or a device, fail with
/// std::errc::invalid_argument; a directory at `to` with is_a_directory. A
/// read-only `to` fails with permission_denied, but for root, and on Windows
/// a hidden one too; on POSIX a `to` of another owner, whose permissions only
/// its owner sets, with operation_not_permitted. These leave `to` as it was.
std::error_code copy_file(std::string_view from, std::string_view to);

/// Creates an empty file in `directory`, named `prefix`, random characters
/// and `suffix`, and none of it there before: the file, open for reading and
/// writing, and its path written into `path`. On POSIX it is its owner's
/// alone (0600), as mkstemp makes it. Nothing removes it. A `prefix` or
/// `suffix` with a separator in it, or on Windows a `:`, which would name a
/// file elsewhere, fails with std::errc::invalid_argument.
std::expected<File, std::error_code> create_temp_file(buffer_ref path,
                                                      std::string_view directory,
                                                      std::string_view prefix,
                                                      std::string_view suffix);

/// Creates an empty directory in `directory`, named `prefix` and random
/// characters, and none of it there before, and writes its path into `path`.
/// On POSIX it is its owner's alone (0700), as mkdtemp makes it. Nothing
/// removes it. `prefix` is refused as create_temp_file() refuses it.
std::error_code create_temp_directory(buffer_ref path,
                                      std::string_view directory,
                                      std::string_view prefix);

}  // namespace kotatsu::fs
