#pragma once

#include <cstdio>
#include <format>
#include <string_view>
#include <utility>

namespace eventide {

template <typename... Args>
void print(std::FILE* stream, std::string_view fmt, Args&&... args) {
    auto out = std::vformat(fmt, std::make_format_args(args...));
    std::fwrite(out.data(), sizeof(char), out.size(), stream);
}

template <typename... Args>
void println(std::FILE* stream, std::string_view fmt, Args&&... args) {
    print(stream, fmt, std::forward<Args>(args)...);
    std::fputc('\n', stream);
}

template <typename... Args>
void print(std::string_view fmt, Args&&... args) {
    print(stdout, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void println(std::string_view fmt, Args&&... args) {
    println(stdout, fmt, std::forward<Args>(args)...);
}

}  // namespace eventide
