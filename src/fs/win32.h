#pragma once

#ifdef _WIN32

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
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
#include <winioctl.h>
#include <winternl.h>

#include "kota/support/buffer_ref.h"
#include "kota/support/small_string.h"
#include "kota/fs/status.h"
#include "kota/fs/volume.h"
#include "kota/fs/windows.h"

namespace kota::fs::win32 {

/// The function `name` of ntdll, which every Windows has, looked up once.
template <typename Function>
Function from_ntdll(std::atomic<Function>& cache, const char* name) {
    auto function = cache.load(std::memory_order_relaxed);
    if(function == nullptr) {
        auto found = ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), name);
        function = reinterpret_cast<Function>(reinterpret_cast<void (*)()>(found));
        cache.store(function, std::memory_order_relaxed);
    }
    return function;
}

/// The Win32 error an NTSTATUS stands for.
DWORD dos_error(NTSTATUS status);

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

/// A wide path, on the stack while it is as short as most are.
using WidePath = small_wstring<264>;

/// Writes into `out` the text a Win32 function writes into a buffer it is
/// given the size of, in UTF-16 units: `fill(buffer, size)` returns how many
/// units it wrote, or when the buffer is too small how many it needs, either
/// with or without the terminator, and 0 on failure. The buffer grows until
/// it fits.
template <typename Fill>
std::error_code fill_wide(basic_buffer_ref<wchar_t> out, Fill fill) {
    auto buffer = out.resize(MAX_PATH);
    while(true) {
        // An empty result succeeds with 0 too, and clears no error itself.
        ::SetLastError(ERROR_SUCCESS);
        auto count =
            static_cast<std::size_t>(fill(buffer.data(), static_cast<DWORD>(buffer.size())));
        if(count == 0) {
            out.clear();
            if(auto error = ::GetLastError(); error != ERROR_SUCCESS) {
                return windows::to_error_code(error);
            }
            return {};
        }
        if(count < buffer.size()) {
            out.resize(count);
            return {};
        }
        // Some functions ask for the size they need, others report a full
        // buffer as an error: either way, a larger one.
        buffer = out.resize(std::max(count + 1, buffer.size() * 2));
    }
}

/// Writes into `out` `path` in UTF-16, as it is: invalid_argument for a NUL
/// in it, which would end it early.
std::error_code widen(basic_buffer_ref<wchar_t> out, std::string_view path);

/// Writes into `out` what GetFullPathNameW resolves `path` to.
std::error_code full_path(basic_buffer_ref<wchar_t> out, const wchar_t* path);

/// A handle to what `name` names, existing, a directory too: the entry of
/// the directory `root`, which no rename meanwhile replaces as it may the
/// directories of a path, or with no `root` the NT path (`\??\C:\x`). A
/// link is followed where `follow` says. Every sharing mode is asked, so
/// that it never refuses another process, and besides `access` the right to
/// read the attributes, as CreateFileW asks it.
///
/// NtCreateFile opens it, which returns why it failed, where Win32 leaves
/// that in the thread's last status, which any call meanwhile may replace.
/// On failure the Win32 error, but for a file deleted while held open: its
/// name stays until the last handle to it goes, but opens nothing, and Win32
/// says ERROR_ACCESS_DENIED, as for a refusal. That fails here with
/// ERROR_DELETE_PENDING, which to_error_code() takes for nothing there.
std::expected<Handle, DWORD>
    open_at(HANDLE root, std::wstring_view name, DWORD access, bool follow);

/// A handle to what the native `path`, as to_native() writes it, names, as
/// open_at() opens it.
std::expected<Handle, DWORD> open_native(std::wstring_view path, DWORD access, bool follow);

/// A handle to what `path` names, symlinks followed, failing with an error
/// code.
std::expected<Handle, std::error_code> open(std::string_view path, DWORD access);

/// Whether a reparse tag makes a link a path is resolved through: a
/// symlink, a junction, a WSL link. Other reparse points, such as cloud
/// placeholders and deduplicated files, hold the file itself.
constexpr bool is_name_surrogate(DWORD tag) {
    return (tag & 0x20000000) != 0;
}

/// Reparse tags older SDKs lack: a Unix socket's entry
/// (IO_REPARSE_TAG_AF_UNIX), and a fifo, a character device and a block
/// device WSL made (IO_REPARSE_TAG_LX_FIFO, _LX_CHR, _LX_BLK).
constexpr DWORD af_unix_tag = 0x80000023;
constexpr DWORD wsl_fifo_tag = 0x80000024;
constexpr DWORD wsl_character_tag = 0x80000025;
constexpr DWORD wsl_block_tag = 0x80000026;

/// The type of a file with these attributes and reparse tag; a junction or
/// a symlink stays itself.
inline FileType type_of(DWORD attributes, DWORD tag) {
    if(attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        if(is_name_surrogate(tag)) {
            return FileType::Symlink;
        }
        switch(tag) {
            case af_unix_tag: return FileType::Socket;
            case wsl_fifo_tag: return FileType::Fifo;
            case wsl_character_tag: return FileType::Character;
            case wsl_block_tag: return FileType::Block;
            default: break;
        }
    }
    return attributes & FILE_ATTRIBUTE_DIRECTORY ? FileType::Directory : FileType::Regular;
}

/// The ID of a file from a source that tells a 64-bit volume serial
/// number, of which FileID keeps the 32 bits every source tells.
inline FileID id_of(std::uint64_t volume, const FILE_ID_128& file) {
    FileID id{.device = static_cast<std::uint32_t>(volume)};
    std::memcpy(&id.file, file.Identifier, sizeof(id.file));
    std::memcpy(&id.file_high, file.Identifier + sizeof(id.file), sizeof(id.file_high));
    return id;
}

/// Writes into `out` the name the OS gives the file `handle` refers to,
/// as canonical() gives it.
std::error_code final_path(buffer_ref out, HANDLE handle);

/// The POSIX permissions Status gives a file with these attributes.
constexpr std::uint16_t permissions_of(DWORD attributes) {
    std::uint16_t readable = attributes & FILE_ATTRIBUTE_DIRECTORY ? 0555 : 0444;
    return attributes & FILE_ATTRIBUTE_READONLY ? readable : readable | 0222;
}

/// Sets the attributes of the file `handle`, open to write them, to
/// `attributes`, a file's times left as they are: false on failure,
/// GetLastError() telling why.
bool set_attributes(HANDLE handle, DWORD attributes);

/// The reparse tag of the file `handle` refers to, which has `attributes`:
/// zero for one that is no reparse point, without asking, since FAT, which
/// keeps none, refuses the question.
std::expected<DWORD, std::error_code> reparse_tag(HANDLE handle, DWORD attributes);

/// The volume the file `handle` refers to lives on, as volume() tells it.
std::expected<Volume, std::error_code> volume_of(HANDLE handle);

/// The ID of the file `handle` refers to.
std::expected<FileID, std::error_code> file_id(HANDLE handle);

/// The status of the file `handle` refers to.
std::expected<Status, std::error_code> handle_status(HANDLE handle);

/// Rewrites `path`, in the form Win32 resolves paths to (to_native() leaves
/// a short absolute one so), in the verbatim form, which any name appended
/// with `\` keeps valid at any length. A device path (`\\.\`) stays as it
/// is but for one too long for Win32, which takes `\\?\`.
void verbatim(basic_buffer_ref<wchar_t> path);

/// FILETIME ticks, 100 ns since 1601, of the Unix epoch.
constexpr std::int64_t unix_epoch_ticks = 116'444'736'000'000'000;

/// FILETIME ticks as a time, the nearest one nanoseconds hold for one
/// before 1677 or after 2262; zero, the time FAT keeps for a change it does
/// not record, is the Unix epoch.
constexpr std::chrono::sys_time<std::chrono::nanoseconds> unix_time(std::int64_t ticks) {
    using Time = std::chrono::sys_time<std::chrono::nanoseconds>;
    constexpr std::int64_t reach = std::numeric_limits<std::int64_t>::max() / 100;
    if(ticks == 0) {
        return Time();
    }
    if(ticks < unix_epoch_ticks - reach) {
        return Time::min();
    }
    if(ticks > unix_epoch_ticks + reach) {
        return Time::max();
    }
    return Time(std::chrono::nanoseconds((ticks - unix_epoch_ticks) * 100));
}

}  // namespace kota::fs::win32

#endif
