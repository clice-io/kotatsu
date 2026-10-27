#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "kota/support/buffer_ref.h"
#include "kota/fs/status.h"
#include "kota/fs/volume.h"

namespace kota::fs {

namespace detail {

/// A directory open for listing, which the DirStream that reads it shares
/// with the entries it gave, as they ask for their statuses.
struct OpenDirectory;

}  // namespace detail

/// An entry of a directory.
struct DirEntry {
    std::string name;

    /// Its type, a link not followed.
    FileType type = FileType::Unknown;

    /// Its status, as symlink_status() gives it. On Windows the listing
    /// holds it, as it was when listed, so that a large directory costs about
    /// what a few statuses do, though without the link count. Elsewhere, and
    /// on a Windows file system that lists no IDs (FAT), the system is asked
    /// when this is called, through the directory, which stays open while an
    /// entry of it lives: an entry removed since fails with
    /// no_such_file_or_directory, and one on FAT that no open reaches gets
    /// what the listing told, without an ID or the link count. An entry made
    /// rather than listed fails with bad_file_descriptor.
    std::expected<Status, std::error_code> status() const;

private:
    friend class DirStream;

    /// The directory to ask its status through; on Windows only where the
    /// listing told not all of it.
    std::shared_ptr<detail::OpenDirectory> directory;

#ifdef _WIN32
    /// What the listing told of the status: all of it, or on a file system
    /// that lists no IDs (FAT) all but the ID and links, which `status()`
    /// then opens the entry for. Empty in an entry made rather than listed.
    std::optional<Status> listed;
#endif
};

/// The entries of a directory, `.` and `..` left out, in the order the
/// system lists them, each read when the stream goes on to it: a range to go
/// through once, of entries, or of the error that ends it. Taking part of it
/// leaves the rest unread, but for the entry after it: std::views::take(n)
/// reads n + 1, as it does of std::views::istream.
class DirStream {
public:
    class iterator {
    public:
        using value_type = std::expected<DirEntry, std::error_code>;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;

        value_type& operator*() const {
            return stream->current;
        }

        iterator& operator++() {
            stream->advance();
            return *this;
        }

        void operator++(int) {
            ++*this;
        }

        bool operator==(std::default_sentinel_t) const {
            return stream->done;
        }

    private:
        friend class DirStream;

        explicit iterator(DirStream* stream) : stream(stream) {}

        DirStream* stream;
    };

    DirStream(DirStream&&) noexcept = default;

    DirStream& operator=(DirStream&&) noexcept = default;

    /// Where the stream stands, its first entry read the first time.
    iterator begin();

    std::default_sentinel_t end() const {
        return {};
    }

    /// The volume the directory lives on, as volume() tells it, asked of the
    /// open directory rather than by name.
    std::expected<Volume, std::error_code> volume() const;

private:
    friend std::expected<DirStream, std::error_code> read_dir(std::string_view dir);

    explicit DirStream(std::shared_ptr<detail::OpenDirectory> directory) :
        directory(std::move(directory)) {}

    /// Reads the next entry into `current`, or after an error, or at the
    /// end, ends the stream.
    void advance();

    std::shared_ptr<detail::OpenDirectory> directory;

    std::expected<DirEntry, std::error_code> current;

    bool started = false;

    bool done = false;
};

/// Opens the directory `dir` to list its entries.
std::expected<DirStream, std::error_code> read_dir(std::string_view dir);

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
/// deletion and the file system allows (NTFS).
std::expected<bool, std::error_code> remove(std::string_view path);

/// Removes `path` and, for a directory, everything in it, never following a
/// symlink or junction into what it points to: the number of entries it
/// removed, `path` included. Nothing there removes nothing; a path ending
/// in `.` or `..` fails with std::errc::invalid_argument.
std::expected<std::uint64_t, std::error_code> remove_all(std::string_view path);

/// Renames `from` to `to`, replacing a file there. On Windows `to` goes even
/// while another process holds it open, where it shares it for deletion and
/// the file system allows (NTFS).
std::error_code rename(std::string_view from, std::string_view to);

/// Renames `from` to `to` where nothing is there: anything there, a dangling
/// symlink too, fails with std::errc::file_exists, and both stay as they
/// were, however processes race, so that of two renames to one name one
/// takes it. `to` on another volume fails with cross_device_link.
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
/// those).
std::error_code read_symlink(buffer_ref out, std::string_view path);

/// Creates `link`, another name of the existing file `target`, which must
/// be no directory; a symlink at `target` gets the name itself, and is not
/// followed.
std::error_code create_hard_link(std::string_view target, std::string_view link);

}  // namespace kota::fs
