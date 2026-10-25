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

std::vector<std::string> split_command_line(std::string_view line) {
    std::vector<std::string> arguments;
    if(line.empty()) {
        return arguments;
    }
    auto blank = [](char c) {
        return c == ' ' || c == '\t';
    };
    std::size_t at = 0;
    auto skip_blanks = [&] {
        while(at < line.size() && blank(line[at])) {
            ++at;
        }
    };

    // The program: no escapes, every quote toggles.
    std::string current;
    bool quoted = false;
    for(; at < line.size(); ++at) {
        char c = line[at];
        if(c == '"') {
            quoted = !quoted;
        } else if(blank(c) && !quoted) {
            break;
        } else {
            current += c;
        }
    }
    arguments.push_back(std::move(current));
    skip_blanks();

    current.clear();
    quoted = false;
    while(at < line.size()) {
        char c = line[at];
        if(blank(c) && !quoted) {
            arguments.push_back(std::move(current));
            current.clear();
            skip_blanks();
            continue;
        }
        if(c == '\\') {
            std::size_t start = at;
            while(at < line.size() && line[at] == '\\') {
                ++at;
            }
            auto count = at - start;
            if(at < line.size() && line[at] == '"') {
                current.append(count / 2, '\\');
                if(count % 2 == 1) {
                    current += '"';
                    ++at;
                }
            } else {
                current.append(count, '\\');
            }
            continue;
        }
        ++at;
        if(c != '"') {
            current += c;
        } else if(!quoted) {
            quoted = true;
        } else if(at == line.size()) {
            // A quote ending the line leaves the argument in quotes.
            break;
        } else if(line[at] == '"') {
            current += '"';
            ++at;
        } else {
            quoted = false;
        }
    }
    // An argument in quotes at the end stands, empty as it may be.
    if(!current.empty() || quoted) {
        arguments.push_back(std::move(current));
    }
    return arguments;
}

}  // namespace kota::sys::windows
