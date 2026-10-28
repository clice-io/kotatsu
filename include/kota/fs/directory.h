#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "kota/support/buffer_ref.h"
#include "kota/fs/file.h"
#include "kota/fs/status.h"
#include "kota/fs/volume.h"

namespace kota::fs {

namespace detail {

/// A directory open for listing, which the Directory that reads it shares
/// with the entries it gave, as they ask for their statuses.
struct OpenDirectory;

}  // namespace detail

/// An open directory, closed when this goes and no entry it gave holds it.
/// Its handle is not inherited by child processes; on Windows, while open,
/// it keeps the directories above it from being renamed.
///
/// It is a range to go through once of the directory's entries, `.` and
/// `..` left out, in the order the system lists them, each read when the
/// range goes on to it, or of the error that ends it. Taking part of it
/// leaves the rest unread, but for the entry after it: std::views::take(n)
/// reads n + 1, as it does of std::views::istream.
class Directory {
public:
    using native_handle_type = File::native_handle_type;

    /// An entry of a directory.
    struct Entry {
        /// Defaulted in directory.cpp: an implicit one would need the
        /// default of `type` before Directory, which holds an Entry, is
        /// complete.
        Entry();

        std::string name;

        /// Its type, a link not followed.
        FileType type = FileType::Unknown;

        /// Its status, as symlink_status() gives it. On Windows the listing
        /// holds it, as it was when listed, so that a large directory costs
        /// about what a few statuses do, though without the link count. NTFS
        /// lists a file's size as it was when a handle to the file last
        /// closed or flushed it: that of a file being written lags behind.
        /// Elsewhere, and on a Windows file system that lists no IDs (FAT),
        /// the system is asked when this is called, through the directory,
        /// which stays open while an entry of it lives: an entry removed
        /// since fails with no_such_file_or_directory, a file deleted while
        /// held open as well, and one on FAT that no open reaches otherwise
        /// gets what the listing told, without an ID or the link count. An
        /// entry made rather than listed fails with bad_file_descriptor.
        std::expected<Status, std::error_code> status() const;

    private:
        friend class Directory;

        /// The directory to ask its status through; on Windows only where
        /// the listing told not all of it.
        std::shared_ptr<detail::OpenDirectory> directory;

#ifdef _WIN32
        /// What the listing told of the status: all of it, or on a file
        /// system that lists no IDs (FAT) all but the ID and links, which
        /// `status()` then opens the entry for. Empty in an entry made rather
        /// than listed.
        std::optional<Status> listed;
#endif
    };

    class Iterator {
    public:
        using value_type = std::expected<Entry, std::error_code>;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;

        value_type& operator*() const {
            return range->current;
        }

        Iterator& operator++() {
            range->advance();
            return *this;
        }

        void operator++(int) {
            ++*this;
        }

        bool operator==(std::default_sentinel_t) const {
            return range->done;
        }

    private:
        friend class Directory;

        explicit Iterator(Directory* range) : range(range) {}

        Directory* range;
    };

    Directory(Directory&&) noexcept = default;

    Directory& operator=(Directory&&) noexcept = default;

    /// Opens the directory at `path`, symlinks followed, to list its
    /// entries: a file fails with std::errc::not_a_directory.
    static std::expected<Directory, std::error_code> open(std::string_view path);

    /// Where the range stands, its first entry read the first time.
    Iterator begin();

    std::default_sentinel_t end() const {
        return {};
    }

    /// The volume the directory lives on, as volume() tells it, asked of the
    /// open directory rather than by name.
    std::expected<Volume, std::error_code> volume() const;

    /// The status of the open directory, as File::status() tells a file's.
    std::expected<Status, std::error_code> status() const;

    /// Writes into `out` the name the OS gives the open directory, as
    /// File::canonical() does a file's.
    std::error_code canonical(buffer_ref out) const;

    /// Waits until the system has written the directory to the disk, where a
    /// crash of the system does not lose it: the files created, removed and
    /// renamed in it, as File::sync() waits for a file's bytes. fsync, on
    /// macOS F_FULLFSYNC. On Windows FlushFileBuffers, through the directory
    /// reopened to write, which a directory the caller may not write fails
    /// with std::errc::permission_denied; whether that reaches a rename,
    /// which Windows offers no way to write through on its own, Windows does
    /// not say.
    std::error_code sync();

    /// Lets go of the directory, which closes once no entry it gave holds
    /// it, the one the range stands on among them. Going on, the range then
    /// gives std::errc::bad_file_descriptor and ends, and what asks the open
    /// directory fails with it. Closing a closed directory does nothing.
    void close();

    /// The descriptor, or on Windows the HANDLE, File::closed once closed;
    /// this keeps it.
    native_handle_type native_handle() const;

private:
    explicit Directory(std::shared_ptr<detail::OpenDirectory> directory) :
        directory(std::move(directory)) {}

    /// Reads the next entry into `current`, or after an error, or at the
    /// end, ends the range.
    void advance();

    std::shared_ptr<detail::OpenDirectory> directory;

    std::expected<Entry, std::error_code> current;

    bool started = false;

    bool done = false;
};

/// A walk of the tree under a directory, which walk() starts: each entry
/// below it, a directory before what it holds, those of one directory in the
/// order the system lists them. A symlink, or on Windows a junction, is an
/// entry of its own, never walked into. Each directory is read whole and
/// closed before the walk goes on, so that it holds none open between
/// entries, however deep the tree.
///
/// It is a range to go through once of the entries, each reached when the
/// range goes on to it, and of the error of each directory it could not
/// read, which it leaves out to go on past it: a root it cannot read gives
/// its error and nothing else.
class Walk {
public:
    struct Options {
        /// How deep to go: the entries of the root are at depth 1, those of
        /// a directory one deeper than it.
        std::size_t max_depth = std::numeric_limits<std::size_t>::max();
    };

    struct Entry {
        /// Defaulted in directory.cpp, for the reason Directory::Entry's is.
        Entry();

        /// The root with the names down to the entry pushed onto it.
        std::string path;

        /// 1 for an entry of the root.
        std::size_t depth = 0;

        /// Its type, a link not followed.
        FileType type = FileType::Unknown;

        /// Its own name, the last of `path`.
        std::string_view name() const {
            return std::string_view(path).substr(name_start);
        }

        /// The names after the root, as `path` spells them.
        std::string_view relative() const {
            return std::string_view(path).substr(relative_start);
        }

    private:
        friend class Walk;

        /// Where the names after the root, and the entry's own name, start
        /// in `path`.
        std::size_t relative_start = 0;

        std::size_t name_start = 0;
    };

    /// A directory the walk could not read.
    struct Error {
        std::string path;

        std::error_code code;
    };

    class Iterator {
    public:
        using value_type = std::expected<Entry, Error>;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;

        value_type& operator*() const {
            return range->current;
        }

        Iterator& operator++() {
            range->advance();
            return *this;
        }

        void operator++(int) {
            ++*this;
        }

        bool operator==(std::default_sentinel_t) const {
            return range->done;
        }

    private:
        friend class Walk;

        explicit Iterator(Walk* range) : range(range) {}

        Walk* range;
    };

    Walk(Walk&&) noexcept = default;

    Walk& operator=(Walk&&) noexcept = default;

    /// Where the range stands, the root read and its first entry reached
    /// the first time.
    Iterator begin();

    std::default_sentinel_t end() const {
        return {};
    }

    /// Leaves out what the directory the range stands on holds, which the
    /// walk then goes past rather than into; at any other entry, or an
    /// error, it does nothing.
    void skip() {
        skipped = true;
    }

private:
    friend Walk walk(std::string_view root, const Options& options);

    /// What a directory listed, still to reach.
    struct Listed {
        std::string name;
        FileType type;
    };

    /// A directory read, whose entries the walk is reaching.
    struct Level {
        std::string path;

        /// The depth of its entries.
        std::size_t depth;

        /// Where its entries start among those listed.
        std::size_t listed;

        /// Where the names after the root start in its path.
        std::size_t relative_start;
    };

    Walk(std::string_view root, const Options& options);

    /// Reads the directory the range stands on, unless skipped or as deep
    /// as the walk goes, and reaches the next entry, or ends the range.
    void advance();

    Options options;

    std::vector<Listed> listed;

    std::vector<Level> levels;

    std::expected<Entry, Error> current;

    bool started = false;

    bool skipped = false;

    bool done = false;
};

/// Walks the tree under the directory `root`, which is followed where it is
/// a symlink.
Walk walk(std::string_view root, const Walk::Options& options = {});

/// Creates the directory `path`: true when it made it, false when a
/// directory was there already.
std::expected<bool, std::error_code> create_directory(std::string_view path);

/// Creates the directory `path` and each missing one above it: true when
/// it made any. Another process making them meanwhile is no error; a file
/// in the way fails with std::errc::not_a_directory, or file_exists when it
/// is `path` itself.
std::expected<bool, std::error_code> create_directories(std::string_view path);

/// Removes a file, a symlink (not what it points to) or an empty
/// directory: true when it removed it, false when nothing was there. On
/// Windows a read-only file goes as well, and the name goes at once, as on
/// POSIX, while another process holds the file open, where it shares it for
/// deletion and the file system allows (NTFS); elsewhere (FAT) it stays
/// until the last handle goes. A file deleted while held open whose name
/// stays so, on any file system, counts as nothing there. A root, and a
/// path whose last name is `.` or `..`, fail with
/// std::errc::invalid_argument, as does on Windows a last name made of dots
/// and spaces alone, which Win32 would take off to leave the directory it
/// is in, unless a separator follows or the path is verbatim.
std::expected<bool, std::error_code> remove(std::string_view path);

/// Removes `path` and, for a directory, everything in it, never following a
/// symlink or junction into what it points to: the number of entries it
/// removed, `path` included. Nothing there removes nothing; a root, or a
/// last name remove() refuses, fails with std::errc::invalid_argument as
/// there. On POSIX a directory the walk is in, moved meanwhile out of the
/// one it came from, fails it with std::errc::no_such_file_or_directory.
std::expected<std::uint64_t, std::error_code> remove_all(std::string_view path);

/// Renames `from` to `to`, replacing a file there. On Windows `to` goes even
/// while another process holds it open, where it shares it for deletion and
/// the file system allows (NTFS). Either path a root, or ending in a name
/// remove() refuses, fails with std::errc::invalid_argument, as there.
std::error_code rename(std::string_view from, std::string_view to);

/// Renames `from` to `to` where nothing is there: anything there, a dangling
/// symlink too, or on Windows a file deleted while held open, whose name
/// stays until the last handle goes, fails with std::errc::file_exists, and
/// both stay as they were, however processes race, so that of two renames
/// to one name one takes it. `to` on another volume fails with
/// cross_device_link; `from` deleted while held open fails as a missing
/// one does, with no_such_file_or_directory; either path a root, or ending
/// in a name remove() refuses, with invalid_argument.
/// MoveFileExW without MOVEFILE_REPLACE_EXISTING on Windows,
/// renameat2(RENAME_NOREPLACE) on Linux, renamex_np(RENAME_EXCL) on macOS.
///
/// Where `to` names the file `from` names, there is no other file to keep,
/// and the systems differ: Linux fails with file_exists; macOS does nothing
/// for `from`'s own name, and fails for another hard link of the file;
/// Windows does nothing for the one, and for the other takes the name `from`
/// away.
///
/// Where the system or the file system has no such rename (NFS, Linux before
/// 3.15, a network file system on macOS), a hard link, which replaces nothing
/// either, gives the file its new name before the old one goes: meanwhile
/// both name it, and both stay after a crash between or should the old one
/// fail to go; of renames of one file to names of their own, more than one
/// may succeed, and leave it each name; and a file another process puts at
/// `from` meanwhile loses its name in the file's place. What takes
/// no hard link there, a directory among others, fails as the system refused
/// the rename: on Linux with std::errc::invalid_argument, as a directory
/// moved into itself does (function_not_supported before 3.15), on macOS
/// with not_supported.
std::error_code rename_noreplace(std::string_view from, std::string_view to);

/// Creates the symlink `link` to the file `target`, which stays as written:
/// a relative target is relative to the link's directory. Windows lets only
/// an administrator or a developer-mode system create one.
std::error_code create_symlink(std::string_view target, std::string_view link);

/// The same for a directory, which Windows tells from a file's.
std::error_code create_directory_symlink(std::string_view target, std::string_view link);

/// Writes into `out` the target of the symlink `path`, as it was written:
/// on Windows with `\` for `/`, as Windows keeps it, and for a junction the
/// directory it names; where the link keeps only the name the system
/// follows, that name. Fails with std::errc::invalid_argument for a file
/// that is no symlink, a Cygwin one included (cygwin::read_symlink reads
/// those), and on Windows with not_supported for a link other than a
/// symlink, a junction or a WSL link, whose target only its own driver
/// knows.
std::error_code read_symlink(buffer_ref out, std::string_view path);

/// Creates `link`, another name of the existing file `target`, which must
/// be no directory; a symlink at `target` gets the name itself, and is not
/// followed.
std::error_code create_hard_link(std::string_view target, std::string_view link);

}  // namespace kota::fs
