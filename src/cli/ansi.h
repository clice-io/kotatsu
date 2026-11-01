#pragma once

#include <format>
#include <string>
#include <string_view>

namespace kotatsu::cli::detail {

constexpr std::string_view ansi_reset = "\033[0m";
constexpr std::string_view ansi_bold = "\033[1m";
constexpr std::string_view ansi_heading = "\033[1;38;5;81m";
constexpr std::string_view ansi_syntax = "\033[1;38;5;117m";
constexpr std::string_view ansi_group = "\033[1;38;5;110m";
constexpr std::string_view ansi_group_title = "\033[1;4;38;5;110m";
constexpr std::string_view ansi_help = "\033[39m";
constexpr std::string_view ansi_error = "\033[1;38;5;203m";
constexpr std::string_view ansi_marker = "\033[1;38;5;204m";

/// `text` in `ansi`, or nothing for no text.
inline std::string paint(std::string_view ansi, std::string_view text) {
    if(text.empty()) {
        return {};
    }
    return std::format("{}{}{}", ansi, text, ansi_reset);
}

}  // namespace kotatsu::cli::detail
