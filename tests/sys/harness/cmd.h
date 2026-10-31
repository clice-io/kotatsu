#pragma once

#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "kotatsu/support/buffer_ref.h"
#include "kotatsu/sys/cmd.h"

namespace kotatsu::test {

/// The arguments cmd::split() reads in `line`.
inline std::vector<std::string> split_all(std::string_view line, sys::cmd::Quoting quoting) {
    std::vector<std::string> arguments;
    for(auto argument: sys::cmd::split(line, quoting)) {
        arguments.emplace_back(argument);
    }
    return arguments;
}

/// The arguments cmd::expand() reads in a response file of `text`.
inline std::vector<std::string> read_response_file(std::string_view text,
                                                   sys::cmd::Quoting quoting) {
    auto read = [&](buffer_ref out, std::string_view) {
        out.assign(text);
        return std::error_code();
    };
    std::string_view command[] = {"@file"};
    std::vector<std::string> arguments;
    for(auto argument: sys::cmd::expand(command, quoting, read)) {
        arguments.emplace_back(argument);
    }
    return arguments;
}

}  // namespace kotatsu::test
