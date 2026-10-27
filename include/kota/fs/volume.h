#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

namespace kota::fs {

/// The file system a file lives on.
struct Volume {
    /// Its size, in bytes.
    std::uint64_t capacity = 0;

    /// What no file holds.
    std::uint64_t free = 0;

    /// What of `free` this user may take: less where the file system keeps
    /// some for the administrator, or a quota limits the user.
    std::uint64_t available = 0;

    /// Its type, as the system names it: "ext4", "btrfs", "tmpfs", "nfs",
    /// "9p", "fuse" on Linux; "apfs", "smbfs" on macOS; "NTFS", "ReFS",
    /// "FAT32" on Windows. On Linux a type it does not know is its magic
    /// number in hex ("0x1234abcd").
    std::string type;

    /// Whether its files are served by another machine, or another system
    /// on this one: NFS, SMB, AFS, Ceph, Lustre, GPFS, and 9p, through which
    /// WSL reaches the Windows drives. FUSE and autofs are not, though what
    /// they serve may be.
    bool remote = false;
};

/// The volume `path`, an existing file, lives on, symlinks followed: on
/// Windows asked of the file opened, which takes one open; Directory::volume
/// asks it of an open directory.
std::expected<Volume, std::error_code> volume(std::string_view path);

}  // namespace kota::fs
