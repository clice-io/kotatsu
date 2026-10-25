#include "kota/sys/console.h"

#include "os.h"

#ifdef _WIN32
#include <algorithm>
#include <io.h>
#include <iterator>

#include "kota/support/utf8.h"
#endif

namespace kota::sys {

#ifdef _WIN32

namespace {

/// `text` through WriteConsoleW, converted a buffer at a time.
std::error_code write_wide(HANDLE console, std::string_view text) noexcept {
    wchar_t buffer[2048];
    while(!text.empty()) {
        std::size_t count = 0;
        // Each code point takes at most two units.
        while(!text.empty() && count + 2 <= std::size(buffer)) {
            auto sequence = decode_utf8(text);
            text.remove_prefix(sequence.length);
            auto code_point = sequence.code_point;
            if(code_point < 0x10000) {
                buffer[count++] = static_cast<wchar_t>(code_point);
            } else {
                code_point -= 0x10000;
                buffer[count++] = static_cast<wchar_t>(0xD800 + (code_point >> 10));
                buffer[count++] = static_cast<wchar_t>(0xDC00 + (code_point & 0x3FF));
            }
        }
        for(const wchar_t* rest = buffer; count > 0;) {
            DWORD written = 0;
            if(!::WriteConsoleW(console, rest, static_cast<DWORD>(count), &written, nullptr)) {
                return fs::windows::last_error();
            }
            rest += written;
            count -= written;
        }
    }
    return {};
}

std::error_code write_bytes(HANDLE handle, std::string_view text) noexcept {
    while(!text.empty()) {
        DWORD written = 0;
        auto size = static_cast<DWORD>(std::min<std::size_t>(text.size(), MAXDWORD));
        if(!::WriteFile(handle, text.data(), size, &written, nullptr)) {
            return fs::windows::last_error();
        }
        text.remove_prefix(written);
    }
    return {};
}

}  // namespace

#endif

std::error_code write_console(int fd, std::string_view text) noexcept {
#ifdef _WIN32
    auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
    if(handle == INVALID_HANDLE_VALUE) {
        return std::make_error_code(std::errc::bad_file_descriptor);
    }
    DWORD mode = 0;
    if(::GetConsoleMode(handle, &mode)) {
        return write_wide(handle, text);
    }
    return write_bytes(handle, text);
#else
    while(!text.empty()) {
        auto written = ::write(fd, text.data(), text.size());
        if(written < 0) {
            if(errno == EINTR) {
                continue;
            }
            return errno_code();
        }
        text.remove_prefix(static_cast<std::size_t>(written));
    }
    return {};
#endif
}

}  // namespace kota::sys
