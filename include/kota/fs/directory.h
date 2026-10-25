#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <memory>
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
    /// what a few statuses do, but no link count. Elsewhere, and on a Windows
    /// file system that lists no IDs (FAT), the system is asked when this is
    /// called, through the directory, which stays open while an entry of it
    /// lives.
    std::expected<Status, std::error_code> status() const;

private:
    friend class DirStream;

    std::shared_ptr<detail::OpenDirectory> directory;

#ifdef _WIN32
    /// What the listing told of the status: all of it, or on a file system
    /// that lists no IDs (FAT) all but the ID and links, which `status()`
    /// then opens the entry for.
    Status listed;
    bool whole = false;
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

        const value_type& operator*() const {
            return stream->current;
        }

        iterator& operator++() {
            stream->advance();
            return *this;
        }

        void operator++(int) {
            ++*this;
        }

        friend bool operator==(const iterator& at, std::default_sentinel_t) {
            return at.at_end();
        }

    private:
        friend class DirStream;

        explicit iterator(DirStream* stream) : stream(stream) {}

        /// A member, unlike the friend above, may read the stream's state.
        bool at_end() const {
            return stream->done;
        }

        DirStream* stream;
    };

    DirStream(DirStream&& other) noexcept = default;

    DirStream& operator=(DirStream&& other) noexcept = default;

    /// Where the stream stands, its first entry read the first time.
    iterator begin();

    std::default_sentinel_t end() const {
        return {};
    }

    /// The volume the directory lives on, as volume() tells it, asked of the
    /// open directory, which takes no other open: whether to list a remote
    /// one, say.
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
