#include "kotatsu/cli/report.h"

#include <cstdio>
#include <string>

#include "kotatsu/sys/console.h"

namespace kotatsu::cli {

Style style_for(int fd) {
    return Style{.color = sys::shows_colors(fd)};
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
