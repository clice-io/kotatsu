#include "kota/fs/volume.h"

#include <utility>

#ifdef _WIN32
#include <cwchar>

#include "win32.h"
#include "kota/support/utf16.h"
#include "kota/fs/resolve.h"
#else
#include <algorithm>
#include <format>

#include "posix.h"
#ifdef __APPLE__
#include <sys/mount.h>
#else
#include <sys/vfs.h>
#endif
#endif

namespace kota::fs {

namespace {

#if !defined(_WIN32) && !defined(__APPLE__)

/// The kinds of file system Linux tells by magic number alone (statfs(2)),
/// with whether their files are served from elsewhere.
struct Kind {
    std::uint32_t magic;
    std::string_view name;
    bool remote = false;
};

constexpr Kind kinds[] = {
    // ext2 and ext3 share ext4's number.
    {0xEF53, "ext4"},
    {0x58465342, "xfs"},
    {0x9123683E, "btrfs"},
    {0x2FC12FC1, "zfs"},
    {0xF2F52010, "f2fs"},
    {0x01021994, "tmpfs"},
    {0x858458F6, "ramfs"},
    {0x794C7630, "overlay"},
    {0x73717368, "squashfs"},
    {0x4D44, "vfat"},
    {0x2011BAB0, "exfat"},
    {0x5346544E, "ntfs"},
    {0x7366746E, "ntfs3"},
    {0x9FA0, "proc"},
    {0x62656572, "sysfs"},
    {0x65735546, "fuse"},
    {0x6969, "nfs", true},
    {0x517B, "smb", true},
    {0xFE534D42, "smb2", true},
    {0xFF534D42, "cifs", true},
    {0x01021997, "9p", true},
    {0x5346414F, "afs", true},
    {0x00C36400, "ceph", true},
};

#endif

}  // namespace

std::expected<Volume, std::error_code> volume(std::string_view path) {
#ifdef _WIN32
    // GetVolumePathNameW reads the path as written: through a symlink only
    // once it has been followed.
    small_string<256> real;
    if(auto error = canonical(real, path)) {
        return std::unexpected(error);
    }
    auto native = win32::native(real);
    if(!native) {
        return std::unexpected(native.error());
    }
    // The root is no longer than the path and the separator it may add.
    win32::WidePath root;
    root.resize(native->size() + 2);
    if(!::GetVolumePathNameW(native->c_str(), root.data(), static_cast<DWORD>(root.size()))) {
        return std::unexpected(windows::last_error());
    }
    root.resize(std::wcslen(root.data()));
    // GetDriveTypeW tells a drive or a share only in its plain spelling.
    small_string<256> spelled;
    windows::from_native(spelled, std::wstring_view(root.data(), root.size()));
    win32::WidePath plain;
    if(auto error = win32::widen(plain, spelled)) {
        return std::unexpected(error);
    }
    ULARGE_INTEGER available;
    ULARGE_INTEGER capacity;
    ULARGE_INTEGER free;
    if(!::GetDiskFreeSpaceExW(plain.c_str(), &available, &capacity, &free)) {
        return std::unexpected(windows::last_error());
    }
    wchar_t type[MAX_PATH + 1];
    if(!::GetVolumeInformationW(plain.c_str(),
                                nullptr,
                                0,
                                nullptr,
                                nullptr,
                                nullptr,
                                type,
                                MAX_PATH + 1)) {
        return std::unexpected(windows::last_error());
    }
    Volume found{
        .capacity = capacity.QuadPart,
        .free = free.QuadPart,
        .available = available.QuadPart,
        .remote = ::GetDriveTypeW(plain.c_str()) == DRIVE_REMOTE,
    };
    from_utf16(found.type, std::wstring_view(type));
    return found;
#else
    auto c_path = posix::c_path(path);
    if(!c_path) {
        return std::unexpected(c_path.error());
    }
    struct statfs info;
    if(::statfs(c_path->c_str(), &info) != 0) {
        return std::unexpected(posix::last_error());
    }
#ifdef __APPLE__
    std::uint64_t unit = info.f_bsize;
    std::string type = info.f_fstypename;
    bool remote = !(info.f_flags & MNT_LOCAL);
#else
    // Blocks count in fragments where the file system has them, as
    // statvfs(3) counts them.
    std::uint64_t unit = info.f_frsize != 0 ? info.f_frsize : info.f_bsize;
    // A signed word: the SMB2 and CIFS numbers have the top bit set.
    auto magic = static_cast<std::uint32_t>(info.f_type);
    const auto* kind = std::ranges::find(kinds, magic, &Kind::magic);
    bool known = kind != std::ranges::end(kinds);
    std::string type = known ? std::string(kind->name) : std::format("{:#x}", magic);
    bool remote = known && kind->remote;
#endif
    return Volume{
        .capacity = info.f_blocks * unit,
        .free = info.f_bfree * unit,
        .available = info.f_bavail * unit,
        .type = std::move(type),
        .remote = remote,
    };
#endif
}

}  // namespace kota::fs
