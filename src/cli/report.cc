#include "kotatsu/cli/report.h"

#include <string>

#include "kotatsu/support/buffer_ref.h"
#include "kotatsu/sys/console.h"
#include "kotatsu/sys/env.h"

namespace kotatsu::cli {

Style style_for(int fd) {
    std::string no_color;
    const bool set = sys::get_env(no_color, "NO_COLOR") && !no_color.empty();
    return Style{.color = sys::is_console(fd) && !set};
}

void report(const Error& error, int fd) {
    detail::write_out(fd, error.render(style_for(fd)) + "\n");
}

void detail::write_out(int fd, std::string_view text) {
    // What could not be written to a closed or full descriptor has nowhere else to go.
    static_cast<void>(sys::write_console(fd, text));
}

}  // namespace kotatsu::cli
