#include "kota/fs/volume.h"

#include <utility>

#ifdef _WIN32
#include <atomic>

#include "win32.h"
#include "kota/support/utf16.h"
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

#if !defined(_WIN32) && !defined(__APPLE__)

namespace {

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

}  // namespace

#endif

#ifdef _WIN32

std::expected<Volume, std::error_code> win32::volume_of(HANDLE handle) {
    // FILE_FS_FULL_SIZE_INFORMATION and FILE_FS_DEVICE_INFORMATION, which
    // only the DDK declares: what GetDiskFreeSpaceExW and GetDriveTypeW ask
    // of a volume's root, asked of the open file, without opening the root.
    struct FullSize {
        LARGE_INTEGER total;
        LARGE_INTEGER caller_available;
        LARGE_INTEGER actual_available;
        ULONG sectors_per_unit;
        ULONG bytes_per_sector;
    };

    struct Device {
        ULONG type;
        ULONG characteristics;
    };

    constexpr int full_size_information = 7;
    constexpr int device_information = 4;
    constexpr ULONG remote_device = 0x10;
    using QueryFn = NTSTATUS(NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, int);
    static constinit std::atomic<QueryFn> query = nullptr;
    auto ask = [&](void* info, ULONG size, int kind) {
        IO_STATUS_BLOCK block{};
        auto status =
            from_ntdll(query, "NtQueryVolumeInformationFile")(handle, &block, info, size, kind);
        return status < 0 ? windows::to_error_code(dos_error(status)) : std::error_code();
    };
    FullSize size;
    if(auto error = ask(&size, sizeof(size), full_size_information)) {
        return std::unexpected(error);
    }
    Device device;
    if(auto error = ask(&device, sizeof(device), device_information)) {
        return std::unexpected(error);
    }
    wchar_t type[MAX_PATH + 1];
    if(!::GetVolumeInformationByHandleW(handle,
                                        nullptr,
                                        0,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        type,
                                        MAX_PATH + 1)) {
        return std::unexpected(windows::last_error());
    }
    auto unit = std::uint64_t(size.sectors_per_unit) * size.bytes_per_sector;
    Volume found{
        .capacity = static_cast<std::uint64_t>(size.total.QuadPart) * unit,
        .free = static_cast<std::uint64_t>(size.actual_available.QuadPart) * unit,
        .available = static_cast<std::uint64_t>(size.caller_available.QuadPart) * unit,
        .type = {},
        .remote = (device.characteristics & remote_device) != 0,
    };
    from_utf16(found.type, std::wstring_view(type));
    return found;
}

#else

namespace {

std::expected<Volume, std::error_code> volume_from(const struct statfs& info) {
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
}

}  // namespace

std::expected<Volume, std::error_code> posix::volume_of(int fd) {
    struct statfs info;
    if(::fstatfs(fd, &info) != 0) {
        return std::unexpected(posix::last_error());
    }
    return volume_from(info);
}

#endif

std::expected<Volume, std::error_code> volume(std::string_view path) {
#ifdef _WIN32
    auto handle = win32::open(path, FILE_READ_ATTRIBUTES);
    if(!handle) {
        return std::unexpected(handle.error());
    }
    return win32::volume_of(handle->get());
#else
    posix::CPath c_path;
    if(auto error = c_path.assign(path)) {
        return std::unexpected(error);
    }
    struct statfs info;
    if(::statfs(c_path.c_str(), &info) != 0) {
        return std::unexpected(posix::last_error());
    }
    return volume_from(info);
#endif
}

}  // namespace kota::fs
