#pragma once

#ifndef _WIN32

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <system_error>

#include "kota/support/small_string.h"
#include "kota/fs/status.h"

namespace kota::fs::posix {

/// errno as an error code.
inline std::error_code last_error() {
    return std::error_code(errno, std::generic_category());
}

/// `path` as the system calls take it, through c_str(), on the stack when it
/// is no longer than most paths are; a NUL inside, which would cut it short,
/// fails.
inline std::expected<small_string<256>, std::error_code> c_path(std::string_view path) {
    if(path.find('\0') != std::string_view::npos) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    return small_string<256>(path);
}

inline Status make_status(const struct stat& info) {
    auto type = S_ISREG(info.st_mode)    ? FileType::Regular
                : S_ISDIR(info.st_mode)  ? FileType::Directory
                : S_ISLNK(info.st_mode)  ? FileType::Symlink
                : S_ISCHR(info.st_mode)  ? FileType::Character
                : S_ISBLK(info.st_mode)  ? FileType::Block
                : S_ISFIFO(info.st_mode) ? FileType::Fifo
                : S_ISSOCK(info.st_mode) ? FileType::Socket
                                         : FileType::Unknown;
#ifdef __APPLE__
    const auto& mtime = info.st_mtimespec;
    const auto& ctime = info.st_ctimespec;
#else
    const auto& mtime = info.st_mtim;
    const auto& ctime = info.st_ctim;
#endif
    auto time_of = [](const struct timespec& time) {
        return std::chrono::sys_time<std::chrono::nanoseconds>(
            std::chrono::seconds(time.tv_sec) + std::chrono::nanoseconds(time.tv_nsec));
    };
    return Status{
        .type = type,
        .size = static_cast<std::uint64_t>(info.st_size),
        .mtime = time_of(mtime),
        .ctime = time_of(ctime),
        .id = {.device = static_cast<std::uint64_t>(info.st_dev),
               .file = static_cast<std::uint64_t>(info.st_ino)},
        .links = static_cast<std::uint32_t>(info.st_nlink),
    };
}

}  // namespace kota::fs::posix

#endif
