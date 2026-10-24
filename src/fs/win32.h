#pragma once

#ifdef _WIN32

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>

#include "kota/fs/status.h"
#include "kota/fs/windows.h"

namespace kota::fs::win32 {

/// A handle closed when this goes.
class Handle {
public:
    Handle() = default;

    explicit Handle(HANDLE value) : value(value) {}

    Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}

    Handle& operator=(Handle&& other) noexcept {
        std::swap(value, other.value);
        return *this;
    }

    ~Handle() {
        if(value != INVALID_HANDLE_VALUE) {
            ::CloseHandle(value);
        }
    }

    HANDLE get() const {
        return value;
    }

private:
    HANDLE value = INVALID_HANDLE_VALUE;
};

/// The text a Win32 function writes into a buffer it is given the size of,
/// in UTF-16 units: `fill(buffer, size)` returns how many units it wrote, or
/// when the buffer is too small how many it needs, either with or without
/// the terminator, and 0 on failure. The buffer grows until it fits.
template <typename Fill>
std::expected<std::wstring, std::error_code> fill_wide(Fill fill) {
    std::wstring buffer(MAX_PATH, L'\0');
    while(true) {
        // An empty result succeeds with 0 too, and clears no error itself.
        ::SetLastError(ERROR_SUCCESS);
        auto count =
            static_cast<std::size_t>(fill(buffer.data(), static_cast<DWORD>(buffer.size())));
        if(count == 0) {
            if(auto error = ::GetLastError(); error != ERROR_SUCCESS) {
                return std::unexpected(windows::to_error_code(error));
            }
            return std::wstring();
        }
        if(count < buffer.size()) {
            buffer.resize(count);
            return buffer;
        }
        // Some functions ask for the size they need, others report a full
        // buffer as an error: either way, a larger one.
        buffer.resize(std::max(count + 1, buffer.size() * 2));
    }
}

/// `path` in UTF-16, as it is: invalid_argument for a NUL in it, which would
/// end it early.
std::expected<std::wstring, std::error_code> widen(std::string_view path);

/// What GetFullPathNameW resolves `path` to.
std::expected<std::wstring, std::error_code> full_path(const std::wstring& path);

/// A handle to what the native `path` names, existing, with every sharing
/// mode so that it never refuses another process, and
/// FILE_FLAG_BACKUP_SEMANTICS, which opening a directory needs, before
/// `flags`. An invalid handle on failure, GetLastError() telling why.
Handle try_open(const std::wstring& path, DWORD access, DWORD flags);

/// A handle to what `path` names, symlinks followed, failing with an error
/// code.
std::expected<Handle, std::error_code> open(std::string_view path, DWORD access);

/// Whether a reparse tag makes a link a path is resolved through: a
/// symlink, a junction, a WSL link. Other reparse points, such as cloud
/// placeholders and deduplicated files, hold the file itself.
constexpr bool is_name_surrogate(DWORD tag) {
    return (tag & 0x20000000) != 0;
}

/// IO_REPARSE_TAG_AF_UNIX, which older SDKs lack: a Unix socket's entry.
constexpr DWORD af_unix_tag = 0x80000023;

/// The type of a file with these attributes and reparse tag; a junction or
/// a symlink stays itself.
inline FileType type_of(DWORD attributes, DWORD tag) {
    if(attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        if(is_name_surrogate(tag)) {
            return FileType::Symlink;
        }
        if(tag == af_unix_tag) {
            return FileType::Socket;
        }
    }
    return attributes & FILE_ATTRIBUTE_DIRECTORY ? FileType::Directory : FileType::Regular;
}

inline FileId id_of(std::uint64_t volume, const FILE_ID_128& file) {
    FileId id{.device = volume};
    std::memcpy(&id.file, file.Identifier, sizeof(id.file));
    std::memcpy(&id.file_high, file.Identifier + sizeof(id.file), sizeof(id.file_high));
    return id;
}

/// The ID of the file `handle` refers to.
std::expected<FileId, std::error_code> file_id(HANDLE handle);

/// `resolved`, a path in the form Win32 resolves paths to (native_path()
/// leaves a short absolute one so), in the verbatim form, which any name
/// appended with `\` keeps valid at any length. A device path stays as it
/// is.
std::wstring verbatim(std::wstring resolved);

/// FILETIME ticks, 100 ns since 1601, as nanoseconds since the Unix epoch;
/// zero, the time FAT keeps for a change it does not record, stays zero.
constexpr std::int64_t unix_ns(std::int64_t ticks) {
    constexpr std::int64_t unix_epoch = 116'444'736'000'000'000;
    return ticks == 0 ? 0 : (ticks - unix_epoch) * 100;
}

}  // namespace kota::fs::win32

#endif
