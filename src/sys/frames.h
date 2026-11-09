#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#endif

#include "os.h"
#include "kotatsu/sys/console.h"

// What each host's write_stacktrace shares: the lines it writes, and on
// arm64 return addresses stripped of their authentication code.

namespace kotatsu::sys {

/// The module an address lies in, as a frame's line names it.
struct FrameModule {
    std::string_view path;
    std::uintptr_t base = 0;
};

/// Writes the lines of a stack trace to a descriptor, through a buffer of
/// its own, allocating nothing.
struct FrameWriter {
    constexpr static int max_frames = 256;

#ifdef _WIN32
    /// The descriptor's handle, looked up once: _get_osfhandle takes nothing
    /// of the C runtime's for a descriptor that is open, where the guard of
    /// descriptor_handle allocates the runtime's data of a thread that has
    /// none.
    HANDLE handle;
#else
    int fd;
#endif
    int frames = 0;
    std::error_code error;
    char buffer[512];
    std::size_t size = 0;

#ifdef _WIN32
    explicit FrameWriter(int fd) noexcept :
        handle(fd < 0 ? INVALID_HANDLE_VALUE : reinterpret_cast<HANDLE>(::_get_osfhandle(fd))) {
        if(handle == INVALID_HANDLE_VALUE) {
            error = std::make_error_code(std::errc::bad_file_descriptor);
        }
    }
#else
    explicit FrameWriter(int fd) noexcept : fd(fd) {}
#endif

    /// Writes the line of the frame at `address`, in `module` or in none,
    /// marked when it is the address a signal or an exception interrupted
    /// rather than a return address: false once no more are to be written,
    /// a write having failed or the last frame written.
    bool frame(std::uintptr_t address,
               std::optional<FrameModule> module,
               bool interrupted) noexcept {
        put_hex(address, 16);
        if(!module) {
            put(" <unknown module>");
        } else {
            put(", ");
            put(module->path);
            put("(");
            put_hex(module->base, 16);
            put(") + ");
            put_hex(address - module->base, 1);
            put(" byte(s)");
        }
        put(interrupted ? " (interrupted)\n" : "\n");
        write(size);
        return !error && ++frames < max_frames;
    }

    void put(std::string_view text) noexcept {
        while(!text.empty()) {
            if(size == sizeof(buffer)) {
                write(whole_characters());
            }
            auto count = std::min(text.size(), sizeof(buffer) - size);
            std::memcpy(buffer + size, text.data(), count);
            size += count;
            text.remove_prefix(count);
        }
    }

    /// `value` as 0x and at least `digits` hexadecimal digits.
    void put_hex(std::uint64_t value, int digits) noexcept {
        char reversed[16];
        int count = 0;
        do {
            reversed[count++] = "0123456789ABCDEF"[value & 0xF];
            value >>= 4;
        } while(value != 0);
        while(count < digits) {
            reversed[count++] = '0';
        }
        char text[18] = {'0', 'x'};
        for(int at = 0; at < count; ++at) {
            text[2 + at] = reversed[count - 1 - at];
        }
        put(std::string_view(text, 2 + count));
    }

    /// The bytes of the buffer before a UTF-8 sequence it holds only the
    /// start of: a Windows console decodes each write apart, and would show
    /// a character two writes split as two that are not.
    std::size_t whole_characters() const noexcept {
        auto continuation = [&](std::size_t at) {
            return (static_cast<unsigned char>(buffer[at]) & 0xC0) == 0x80;
        };
        std::size_t lead = size;
        while(lead > 0 && size - lead < 3 && continuation(lead - 1)) {
            --lead;
        }
        if(lead == 0) {
            return size;
        }
        auto byte = static_cast<unsigned char>(buffer[lead - 1]);
        std::size_t length = byte >= 0xF0 ? 4 : byte >= 0xE0 ? 3 : byte >= 0xC0 ? 2 : 1;
        return lead - 1 + length > size ? lead - 1 : size;
    }

    /// Writes the first `count` bytes of the buffer, keeping the rest.
    void write(std::size_t count) noexcept {
        std::string_view text(buffer, count);
        while(!error && !text.empty()) {
#ifdef _WIN32
            auto written = write_handle_some(handle, text);
#else
            auto written = write_console_some(fd, text);
#endif
            if(written) {
                text.remove_prefix(*written);
            } else {
                error = written.error();
            }
        }
        std::memmove(buffer, buffer + count, size - count);
        size -= count;
    }
};

#if defined(__aarch64__) && !defined(_WIN32)
/// `address` without the pointer authentication code a return address
/// saved by code built with `-mbranch-protection` carries: XPACLRI, a hint
/// that does nothing on a CPU without it.
inline std::uintptr_t strip_return_address(std::uintptr_t address) noexcept {
    register std::uintptr_t x30 asm("x30") = address;
    asm("hint #7" : "+r"(x30));
    return x30;
}
#endif

}  // namespace kotatsu::sys
