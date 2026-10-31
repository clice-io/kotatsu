#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "kotatsu/sys/windows.h"

namespace kotatsu::sys::windows {

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

/// Reads the backslashes at `at` of `line` into `argument`: 2n of them
/// before a quote make n, and leave the quote to be read; 2n+1 make n and a
/// quote, which they take; others are as they are. The index of the last
/// character taken.
std::size_t read_backslashes(std::string_view line, std::size_t at, small_string<256>& argument) {
    std::size_t start = at;
    while(at < line.size() && line[at] == '\\') {
        ++at;
    }
    auto count = at - start;
    if(at == line.size() || line[at] != '"') {
        argument.append(count, '\\');
        return at - 1;
    }
    argument.append(count / 2, '\\');
    if(count % 2 == 0) {
        return at - 1;
    }
    argument.push_back('"');
    return at;
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

split_command_line_view::iterator::iterator(std::string_view line, CommandLineRules rules) :
    rest(line), rules(rules), program(rules != CommandLineRules::LLVM) {
    ++*this;
}

split_command_line_view::iterator& split_command_line_view::iterator::operator++() {
    const bool crt = rules == CommandLineRules::CRT;
    // The C runtime's program starts at the first character, a blank too,
    // and reads every quote as a toggle.
    const bool crt_program = crt && program;
    auto separates = [&](char c) {
        return c == ' ' || c == '\t' || (!crt && (c == '\r' || c == '\n' || c == '\0'));
    };
    std::size_t at = 0;
    if(!crt_program) {
        while(at < rest.size() && separates(rest[at])) {
            ++at;
        }
    }
    if(at == rest.size()) {
        done = true;
        return *this;
    }
    argument.clear();
    bool quoted = false;
    // It ends at a separator outside quotes, or at the end of the line, in
    // quotes or not.
    for(; at < rest.size() && (quoted || !separates(rest[at])); ++at) {
        char c = rest[at];
        if(c == '\\' && !program) {
            at = read_backslashes(rest, at, argument);
        } else if(c != '"') {
            argument.push_back(c);
        } else if(quoted && !crt_program && at + 1 < rest.size() && rest[at + 1] == '"') {
            argument.push_back('"');
            ++at;
        } else {
            quoted = !quoted;
        }
    }
    // As LLVM reads it, a command starts after a newline that ends an
    // argument, not after one that a blank comes before.
    program = rules == CommandLineRules::LLVMFull && at < rest.size() && rest[at] == '\n';
    rest.remove_prefix(at);
    return *this;
}

}  // namespace kotatsu::sys::windows
