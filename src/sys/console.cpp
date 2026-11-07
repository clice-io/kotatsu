#include "kotatsu/sys/console.h"

#include "os.h"

#ifdef _WIN32
#include <algorithm>
#include <iterator>

#include "kotatsu/support/utf8.h"
#else
#include <fcntl.h>
#include <sys/stat.h>
#endif

namespace kotatsu::sys {

#ifdef _WIN32

namespace {

bool is_console_handle(HANDLE handle) noexcept {
    DWORD mode = 0;
    return ::GetConsoleMode(handle, &mode) != 0;
}

/// The start of `text` through WriteConsoleW, as many code points as a
/// buffer holds: the bytes of those it wrote.
std::expected<std::size_t, std::error_code> write_wide(HANDLE console,
                                                       std::string_view text) noexcept {
    wchar_t buffer[2048];
    std::size_t count = 0;
    // Each code point takes at most two units.
    for(auto rest = text; !rest.empty() && count + 2 <= std::size(buffer);) {
        auto sequence = decode_utf8(rest);
        rest.remove_prefix(sequence.length);
        auto code_point = sequence.code_point;
        if(code_point < 0x10000) {
            buffer[count++] = static_cast<wchar_t>(code_point);
        } else {
            code_point -= 0x10000;
            buffer[count++] = static_cast<wchar_t>(0xD800 + (code_point >> 10));
            buffer[count++] = static_cast<wchar_t>(0xDC00 + (code_point & 0x3FF));
        }
    }
    DWORD written = 0;
    if(!::WriteConsoleW(console, buffer, static_cast<DWORD>(count), &written, nullptr)) {
        return std::unexpected(fs::windows::last_error());
    }
    std::size_t bytes = 0;
    for(std::size_t units = 0; units < written;) {
        auto sequence = decode_utf8(text.substr(bytes));
        bytes += sequence.length;
        units += sequence.code_point < 0x10000 ? 1 : 2;
    }
    return bytes;
}

}  // namespace

#endif

std::error_code write_console(int fd, std::string_view text) noexcept {
    while(!text.empty()) {
        auto written = write_console_some(fd, text);
        if(!written) {
            return written.error();
        }
        text.remove_prefix(*written);
    }
    return {};
}

std::expected<std::size_t, std::error_code> write_console_some(int fd,
                                                               std::string_view text) noexcept {
    if(text.empty()) {
        return 0;
    }
#ifdef _WIN32
    auto handle = descriptor_handle(fd);
    if(handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
    }
    if(is_console_handle(handle)) {
        return write_wide(handle, text);
    }
    DWORD written = 0;
    auto size = static_cast<DWORD>(std::min<std::size_t>(text.size(), MAXDWORD));
    if(!::WriteFile(handle, text.data(), size, &written, nullptr)) {
        return std::unexpected(fs::windows::last_error());
    }
    // What a full pipe in PIPE_NOWAIT mode does, where POSIX says EAGAIN.
    if(written == 0) {
        return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
    }
    return written;
#else
    while(true) {
        auto written = ::write(fd, text.data(), text.size());
        if(written >= 0) {
            return static_cast<std::size_t>(written);
        }
        if(errno != EINTR) {
            return std::unexpected(errno_code());
        }
    }
#endif
}

std::error_code set_nonblocking(int fd) noexcept {
#ifdef _WIN32
    auto handle = descriptor_handle(fd);
    if(handle == INVALID_HANDLE_VALUE) {
        return std::make_error_code(std::errc::bad_file_descriptor);
    }
    if(::GetFileType(handle) != FILE_TYPE_PIPE) {
        return std::make_error_code(std::errc::not_supported);
    }
    DWORD state = 0;
    if(!::GetNamedPipeHandleState(handle, &state, nullptr, nullptr, nullptr, nullptr, 0)) {
        return fs::windows::last_error();
    }
    // Both modes are set at once: the read mode stays.
    DWORD mode = (state & PIPE_READMODE_MESSAGE) | PIPE_NOWAIT;
    if(!::SetNamedPipeHandleState(handle, &mode, nullptr, nullptr)) {
        if(::GetLastError() == ERROR_PIPE_BUSY) {
            return std::make_error_code(std::errc::device_or_resource_busy);
        }
        return fs::windows::last_error();
    }
    return {};
#else
    int flags = ::fcntl(fd, F_GETFL);
    if(flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        return errno_code();
    }
    return {};
#endif
}

bool is_console(int fd) noexcept {
#ifdef _WIN32
    return is_console_handle(descriptor_handle(fd));
#else
    return ::isatty(fd) == 1;
#endif
}

bool is_pipe(int fd) noexcept {
#ifdef _WIN32
    return ::GetFileType(descriptor_handle(fd)) == FILE_TYPE_PIPE;
#else
    struct stat status;
    return ::fstat(fd, &status) == 0 && (S_ISFIFO(status.st_mode) || S_ISSOCK(status.st_mode));
#endif
}

}  // namespace kotatsu::sys
