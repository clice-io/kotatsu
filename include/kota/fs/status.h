#pragma once

#include <cstdint>
#include <expected>
#include <string_view>
#include <system_error>

namespace kota::fs {

/// What a file is.
enum class FileType : std::uint8_t {
    Regular,
    Directory,
    /// A symbolic link; on Windows also a junction or any other reparse
    /// point a path is resolved through (a WSL link), but not one that holds
    /// the file itself (a cloud placeholder).
    Symlink,
    Character,
    Block,
    Fifo,
    Socket,
    Unknown,
};

/// What tells files apart while they exist: the device, and the file's
/// number on it. On Windows the volume's serial number and the 128-bit file
/// ID, which ReFS needs whole. All zero where the source of a status does
/// not tell it.
struct FileId {
    std::uint64_t device = 0;
    std::uint64_t file = 0;

    /// The high half of a 128-bit file ID: zero on POSIX and NTFS.
    std::uint64_t file_high = 0;

    friend bool operator==(const FileId&, const FileId&) = default;
};

/// What a status call says about a file.
struct Status {
    FileType type = FileType::Unknown;

    /// In bytes.
    std::uint64_t size = 0;

    /// When its bytes last changed, in nanoseconds since the Unix epoch.
    std::int64_t mtime_ns = 0;

    /// When its bytes or its metadata last changed (POSIX ctime, Windows
    /// ChangeTime): unlike the mtime nothing sets it back, so it moves when
    /// a file is replaced under its name with its size and mtime kept
    /// (`cp -p`). Zero where the file system keeps none (FAT).
    std::int64_t ctime_ns = 0;

    FileId id = {};

    /// The file's hard links; zero where the source does not tell.
    std::uint32_t links = 0;

    friend bool operator==(const Status&, const Status&) = default;
};

/// The status of what `path` names, symlinks followed.
///
/// Windows has no status by name before Windows 11 24H2, where it is taken
/// by name; elsewhere it opens the file, asking for no access, so that no
/// sharing mode refuses it. A file no open reaches, such as a running
/// system's page file, is asked through its directory's listing instead,
/// which tells no change time, ID or links.
std::expected<Status, std::error_code> status(std::string_view path);

/// The status of `path` itself: a symlink, or a junction on Windows, is not
/// followed.
std::expected<Status, std::error_code> symlink_status(std::string_view path);

/// Whether `path` names anything, symlinks followed: false when the OS says
/// nothing is there (a dangling symlink, a component that is no
/// directory), an error when it cannot tell, such as when it denies access.
std::expected<bool, std::error_code> exists(std::string_view path);

/// Whether `path` names a regular file, symlinks followed; false on any
/// error.
bool is_file(std::string_view path);

/// Whether `path` names a directory, symlinks followed; false on any error.
bool is_directory(std::string_view path);

/// Whether `path` is itself a symlink (see FileType::Symlink); false on any
/// error.
bool is_symlink(std::string_view path);

/// Whether `a` and `b` name the same file, symlinks followed: equal file IDs.
std::expected<bool, std::error_code> equivalent(std::string_view a, std::string_view b);

}  // namespace kota::fs
