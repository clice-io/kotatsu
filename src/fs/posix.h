#pragma once

#ifndef _WIN32

#include <cerrno>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <system_error>

#include "kota/fs/status.h"

namespace kota::fs::posix {

/// errno as an error code.
inline std::error_code last_error() {
    return std::error_code(errno, std::generic_category());
}

/// `path` as the system calls take it, NUL-terminated; a NUL inside, which
/// would cut it short, fails.
inline std::expected<std::string, std::error_code> c_path(std::string_view path) {
    if(path.find('\0') != std::string_view::npos) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    return std::string(path);
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
    auto ns = [](const struct timespec& time) {
        return static_cast<std::int64_t>(time.tv_sec) * 1'000'000'000 + time.tv_nsec;
    };
    return Status{
        .type = type,
        .size = static_cast<std::uint64_t>(info.st_size),
        .mtime_ns = ns(mtime),
        .ctime_ns = ns(ctime),
        .id = {.device = static_cast<std::uint64_t>(info.st_dev),
               .file = static_cast<std::uint64_t>(info.st_ino)},
        .links = static_cast<std::uint32_t>(info.st_nlink),
    };
}

}  // namespace kota::fs::posix

#endif
