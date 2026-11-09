#pragma once

#include <string_view>

#include "kotatsu/cli/error.h"
#include "kotatsu/cli/usage.h"
#include "kotatsu/support/string_list_ref.h"

namespace kotatsu::cli {

/// The style for writing to descriptor `fd`: in color when sys::shows_colors(fd).
Style style_for(int fd);

/// Writes `error`, rendered in the style for `fd`, and a newline to descriptor `fd`, the
/// standard error by default: what a program says when its argv does not parse.
void report(const Error& error, int fd = 2);

namespace detail {

/// Writes `text` to descriptor `fd`, to a Windows console as the UTF-16 it shows.
void write_out(int fd, std::string_view text);

}  // namespace detail

/// Writes usage<T>(), in the style for `fd`, to descriptor `fd`, the standard output by
/// default: what a program says for --help, of the command its Help names.
template <typename T>
void print_usage(std::string_view program, string_list_ref subcommands = {}, int fd = 1) {
    detail::write_out(fd, usage<T>(program, subcommands, style_for(fd)));
}

}  // namespace kotatsu::cli
