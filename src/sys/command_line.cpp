#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "kota/sys/windows.h"

namespace kota::sys::windows {

namespace {

/// Appends `argument` to `out`, quoted as quote_argument() quotes it.
void append_quoted(buffer_ref out, std::string_view argument) {
    if(!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string_view::npos) {
        out.append(argument);
        return;
    }
    out.push_back('"');
    std::size_t backslashes = 0;
    auto put_backslashes = [&](std::size_t count) {
        for(std::size_t i = 0; i < count; ++i) {
            out.push_back('\\');
        }
    };
    for(char c: argument) {
        if(c == '\\') {
            ++backslashes;
            continue;
        }
        // Backslashes before a quote escape themselves, and one more the quote.
        put_backslashes(c == '"' ? backslashes * 2 + 1 : backslashes);
        backslashes = 0;
        out.push_back(c);
    }
    // The closing quote must not be escaped.
    put_backslashes(backslashes * 2);
    out.push_back('"');
}

}  // namespace

void quote_argument(buffer_ref out, std::string_view argument) {
    out.clear();
    append_quoted(out, argument);
}

void join_command_line(buffer_ref out, string_list_ref arguments) {
    out.clear();
    for(std::size_t i = 0; i < arguments.size(); ++i) {
        auto argument = arguments[i];
        if(i != 0) {
            out.push_back(' ');
            append_quoted(out, argument);
            continue;
        }
        assert(argument.find('"') == std::string_view::npos && "a program name holds no quote");
        bool quote = argument.empty() || argument.find_first_of(" \t") != std::string_view::npos;
        if(quote) {
            out.push_back('"');
        }
        out.append(argument);
        if(quote) {
            out.push_back('"');
        }
    }
}

split_command_line_view::iterator::iterator(std::string_view line) : rest(line) {
    if(line.empty()) {
        done = true;
        return;
    }
    ++*this;
}

split_command_line_view::iterator& split_command_line_view::iterator::operator++() {
    auto blank = [](char c) {
        return c == ' ' || c == '\t';
    };
    if(!program && rest.empty()) {
        done = true;
        return *this;
    }
    argument.clear();
    bool quoted = false;
    std::size_t at = 0;
    if(program) {
        // No escapes, every quote toggles.
        program = false;
        for(; at < rest.size() && (quoted || !blank(rest[at])); ++at) {
            if(rest[at] == '"') {
                quoted = !quoted;
            } else {
                argument.push_back(rest[at]);
            }
        }
    } else {
        // It starts at what is no blank, and ends at a blank outside quotes,
        // or at the end of the line, in quotes or not.
        while(at < rest.size() && (quoted || !blank(rest[at]))) {
            char c = rest[at];
            if(c == '\\') {
                std::size_t start = at;
                while(at < rest.size() && rest[at] == '\\') {
                    ++at;
                }
                auto count = at - start;
                if(at < rest.size() && rest[at] == '"') {
                    argument.append(count / 2, '\\');
                    if(count % 2 == 1) {
                        argument.push_back('"');
                        ++at;
                    }
                } else {
                    argument.append(count, '\\');
                }
                continue;
            }
            ++at;
            if(c != '"') {
                argument.push_back(c);
            } else if(!quoted) {
                quoted = true;
            } else if(at < rest.size() && rest[at] == '"') {
                argument.push_back('"');
                ++at;
            } else {
                quoted = false;
            }
        }
    }
    auto next = rest.find_first_not_of(" \t", at);
    rest = next == std::string_view::npos ? std::string_view() : rest.substr(next);
    return *this;
}

}  // namespace kota::sys::windows
