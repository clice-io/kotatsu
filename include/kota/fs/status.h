#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
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
/// number on it. On Windows the volume's serial number in the 32 bits every
/// source of a status reports, as LLVM, libuv and Rust take it, though NTFS
/// and ReFS keep 64; and the 128-bit file ID, which ReFS needs whole. All
/// zero where the source of a status does not tell it.
struct FileID {
    std::uint64_t device = 0;
    std::uint64_t file = 0;

    /// The high half of a 128-bit file ID: zero on POSIX and NTFS.
    std::uint64_t file_high = 0;

    friend bool operator==(const FileID&, const FileID&) = default;
};

/// What a status call says about a file.
struct Status {
    FileType type = FileType::Unknown;

    /// In bytes.
    std::uint64_t size = 0;

    /// When its bytes last changed. Here and in `ctime`, a time before 1677
    /// or after 2262, past what nanoseconds since 1970 hold, is the nearest
    /// one they do.
    std::chrono::sys_time<std::chrono::nanoseconds> mtime{};

    /// When its bytes or its metadata last changed (POSIX ctime, Windows
    /// ChangeTime): unlike the mtime nothing sets it back, so it moves when
    /// a file is replaced under its name with its size and mtime kept
    /// (`cp -p`). The epoch where the file system keeps none (FAT).
    std::chrono::sys_time<std::chrono::nanoseconds> ctime{};

    FileID id = {};

    /// The file's hard links; zero where the source does not tell.
    std::uint32_t links = 0;

    /// Who may read, write and run it, as POSIX mode bits (0644, 0755) with
    /// setuid, setgid and sticky (07777). Windows keeps only whether a file
    /// is read-only: 0444 then, else 0666, and the run bits (0111) for a
    /// directory; a device or a pipe there tells none, zero.
    std::uint16_t permissions = 0;

    friend bool operator==(const Status&, const Status&) = default;
};

/// The status of what `path` names, symlinks followed.
///
/// Windows has a status by name from Windows 11 24H2 on, which this takes
/// but for a link to follow or a file on another machine; otherwise it
/// opens the file, asking for no access, so that no sharing mode refuses
/// it. A file no open reaches, such as a running system's page file, is
/// asked through its directory's listing instead, which tells no change
/// time, ID or links.
std::expected<Status, std::error_code> status(std::string_view path);

/// The status of `path` itself: a symlink, or a junction on Windows, is not
/// followed, even to a directory with a separator after it (`link/`),
/// through which POSIX follows it otherwise.
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

/// Whether `a` and `b` name the same file, symlinks followed: equal file
/// IDs. Fails with std::errc::not_supported for a file without one: a
/// device, a pipe, a file only a directory listing reaches.
std::expected<bool, std::error_code> equivalent(std::string_view a, std::string_view b);

/// The times set_times() sets; one left empty stays as it is.
struct FileTimes {
    /// When the file was last read.
    std::optional<std::chrono::sys_time<std::chrono::nanoseconds>> atime = std::nullopt;

    /// When its bytes last changed, Status::mtime.
    std::optional<std::chrono::sys_time<std::chrono::nanoseconds>> mtime = std::nullopt;
};

/// Sets the times of what `path` names, a directory too, symlinks followed.
/// Windows keeps them in 100 ns, to which they are rounded down; a file
/// system keeps what it can of that (FAT32 and exFAT an mtime in two
/// seconds, rounded up).
std::error_code set_times(std::string_view path, const FileTimes& times);

/// Sets who may read, write and run what `path` names, symlinks followed, to
/// the POSIX mode bits `permissions`, at most 07777, as Status::permissions
/// tells them. On Windows only whether a file is read-only: without a write
/// bit (0222) it is, otherwise it is not.
std::error_code set_permissions(std::string_view path, std::uint16_t permissions);

}  // namespace kota::fs
