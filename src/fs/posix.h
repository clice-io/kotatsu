#pragma once

#ifndef _WIN32

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <expected>
#include <string_view>
#include <sys/stat.h>
#include <system_error>

#include "kotatsu/support/small_string.h"
#include "kotatsu/fs/status.h"
#include "kotatsu/fs/volume.h"

namespace kotatsu::fs::posix {

/// errno as an error code.
inline std::error_code last_error() {
    return std::error_code(errno, std::generic_category());
}

/// A path as the system calls take it, NUL-terminated, on the stack while
/// it is no longer than most paths are.
class CPath {
public:
    /// Takes `path`: invalid_argument for a NUL inside, which would cut it
    /// short.
    std::error_code assign(std::string_view path) {
        if(path.find('\0') != std::string_view::npos) {
            return std::make_error_code(std::errc::invalid_argument);
        }
        text.assign(path);
        return {};
    }

    const char* c_str() {
        return text.c_str();
    }

private:
    small_string<256> text;
};

/// The volume the file `fd` refers to lives on, as volume() tells it.
std::expected<Volume, std::error_code> volume_of(int fd);

/// `path` without its trailing separators, its root kept: through them the
/// system follows a symlink the path ends in.
inline std::string_view without_trailing_separators(std::string_view path) {
    auto end = path.size();
    while(end > 1 && path[end - 1] == '/') {
        --end;
    }
    return path.substr(0, end);
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
    // A time nanoseconds do not hold, before 1677 or after 2262, is the
    // nearest one they do. Before 1970 a second moves into the nanoseconds,
    // so that the earliest seconds do not overflow before they are added.
    auto time_of = [](const struct timespec& time) {
        using Time = std::chrono::sys_time<std::chrono::nanoseconds>;
        std::int64_t seconds = time.tv_sec;
        std::int64_t nanoseconds = time.tv_nsec;
        if(seconds < 0) {
            seconds += 1;
            nanoseconds -= 1'000'000'000;
        }
        std::int64_t since;
        if(__builtin_mul_overflow(seconds, 1'000'000'000, &since) ||
           __builtin_add_overflow(since, nanoseconds, &since)) {
            return time.tv_sec < 0 ? Time::min() : Time::max();
        }
        return Time(std::chrono::nanoseconds(since));
    };
    return Status{
        .type = type,
        .size = static_cast<std::uint64_t>(info.st_size),
        .mtime = time_of(mtime),
        .ctime = time_of(ctime),
        .id = {.device = static_cast<std::uint64_t>(info.st_dev),
               .file = static_cast<std::uint64_t>(info.st_ino)},
        .links = static_cast<std::uint32_t>(info.st_nlink),
        .permissions = static_cast<std::uint16_t>(info.st_mode & 07777),
    };
}

}  // namespace kotatsu::fs::posix

#endif
