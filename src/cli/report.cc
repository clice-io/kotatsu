#include "kotatsu/cli/report.h"

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <io.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "kotatsu/support/buffer_ref.h"
#include "kotatsu/sys/console.h"
#include "kotatsu/sys/env.h"

namespace kotatsu::cli {

namespace {

/// Whether a terminal at `fd` shows ANSI colors: a Windows console only with virtual terminal
/// processing on, which it is in Windows Terminal and not in an older console host.
bool shows_colors(int fd) {
    if(!sys::is_console(fd)) {
        return false;
    }
#ifdef _WIN32
    DWORD mode = 0;
    const auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
    return ::GetConsoleMode(handle, &mode) != 0 && (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return true;
#endif
}

}  // namespace

Style style_for(int fd) {
    std::string no_color;
    const bool set = sys::get_env(no_color, "NO_COLOR") && !no_color.empty();
    return Style{.color = !set && shows_colors(fd)};
}

void report(const Error& error, int fd) {
    detail::write_out(fd, error.render(style_for(fd)) + "\n");
}

void detail::write_out(int fd, std::string_view text) {
    // What stdio holds goes first, so that the text lands after what was printed before it.
    std::fflush(nullptr);
    // What could not be written to a closed or full descriptor has nowhere else to go.
    static_cast<void>(sys::write_console(fd, text));
}

}  // namespace kotatsu::cli
