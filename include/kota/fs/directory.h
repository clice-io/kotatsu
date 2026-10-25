#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "kota/support/buffer_ref.h"
#include "kota/fs/status.h"

namespace kota::fs {

/// An entry of a directory: its name, and its type with a link not
/// followed.
struct DirEntry {
    std::string name;
    FileType type = FileType::Unknown;
};

/// The entries of a directory, `.` and `..` left out, in the order the
/// system lists them.
std::expected<std::vector<DirEntry>, std::error_code> read_dir(std::string_view dir);

/// An entry of a directory with its status, a link not followed.
struct StatusEntry {
    std::string name;
    Status status;
};

/// The entries of a directory with their statuses, as symlink_status()
/// would give them. On Windows the listing itself holds them, so a large
/// directory costs about what a few statuses do; it tells no link counts.
///
/// At most `limit` entries: the listing stops there, and the rest goes
/// unread. A caller that gives up on a directory past some size asks for
/// one entry more, to see whether it is.
std::expected<std::vector<StatusEntry>, std::error_code>
    read_dir_status(std::string_view dir,
                    std::size_t limit = std::numeric_limits<std::size_t>::max());

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
