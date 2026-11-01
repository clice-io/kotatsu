#pragma once

#include <string_view>

#include "kotatsu/cli/error.h"
#include "kotatsu/cli/usage.h"

namespace kotatsu::cli {

/// The style for writing to descriptor `fd`: in color when it is a terminal and the
/// environment sets no NO_COLOR.
Style style_for(int fd);

/// Writes `error`, rendered in the style for `fd`, and a newline to descriptor `fd`, the
/// standard error by default: what a program says when its argv does not parse.
void report(const Error& error, int fd = 2);

namespace detail {

/// Writes `text` to descriptor `fd`, to a Windows console as the UTF-16 it shows.
void write_out(int fd, std::string_view text);

}  // namespace detail

/// Writes the usage of the options `T` declares, in the style for `fd`, to descriptor `fd`,
/// the standard output by default: what a program says for --help.
template <typename T>
void print_usage(std::string_view usage_line, int fd = 1) {
    detail::write_out(fd, usage<T>(usage_line, style_for(fd)));
}

}  // namespace kotatsu::cli
