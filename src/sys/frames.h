#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <system_error>

#include "kotatsu/sys/console.h"

// What each host's write_stacktrace shares: the lines it writes.

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

    int fd;
    int frames = 0;
    std::error_code error = {};
    char buffer[512];
    std::size_t size = 0;

    explicit FrameWriter(int fd) noexcept : fd(fd) {}

    /// Writes the line of the frame at `address`, in `module`, or in none
    /// when it is null, marked when it is the address a signal or an
    /// exception interrupted rather than a return address: false once no
    /// more are to be written, a write having failed or the last frame
    /// written.
    bool frame(std::uintptr_t address, const FrameModule* module, bool interrupted) noexcept {
        put_hex(address, 16);
        if(module == nullptr) {
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
        flush();
        return !error && ++frames < max_frames;
    }

    void put(std::string_view text) noexcept {
        while(!text.empty()) {
            if(size == sizeof(buffer)) {
                flush();
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

    void flush() noexcept {
        if(!error && size != 0) {
            error = write_console(fd, std::string_view(buffer, size));
        }
        size = 0;
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
