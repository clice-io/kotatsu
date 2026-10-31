#include "kotatsu/sys/cmd.h"

#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "kotatsu/support/utf16.h"

namespace kotatsu::sys::cmd {

namespace {

/// Appends `program` to `out` as join() writes a Windows program.
void append_windows_program(buffer_ref out, std::string_view program) {
    assert(!program.contains('"') && "a program name holds no quote");
    bool quote = program.empty() || program.find_first_of(" \t") != std::string_view::npos;
    if(quote) {
        out.push_back('"');
    }
    out.append(program);
    if(quote) {
        out.push_back('"');
    }
}

/// Appends `argument` to `out` as join() writes a Windows argument after the
/// program.
void append_windows(buffer_ref out, std::string_view argument) {
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

/// Appends `argument` to `out` as join() writes a POSIX argument.
void append_posix(buffer_ref out, std::string_view argument) {
    if(argument.empty()) {
        out.append("''");
        return;
    }
    for(char c: argument) {
        if(c == '\n') {
            out.append("'\n'");
            continue;
        }
        bool plain = static_cast<unsigned char>(c) >= 0x80 || (c >= 'a' && c <= 'z') ||
                     (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                     std::string_view("@%+=:,./_-").contains(c);
        if(!plain) {
            out.push_back('\\');
        }
        out.push_back(c);
    }
}

/// Reads the backslashes at `at` of `line` into `argument` by the C
/// runtime's rules: 2n of them before a quote make n, and leave the quote to
/// be read; 2n+1 make n and a quote, which they take; others are as they
/// are. The index of the last character taken.
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

/// Turns the text of a response file into the UTF-8 it is read as: drops a
/// UTF-8 byte order mark, and converts UTF-16 that starts with one. False
/// for UTF-16 of an odd number of bytes.
bool decode(std::string& text) {
    if(text.starts_with("\xEF\xBB\xBF")) {
        text.erase(0, 3);
        return true;
    }
    bool little = text.starts_with("\xFF\xFE");
    if(!little && !text.starts_with("\xFE\xFF")) {
        return true;
    }
    if(text.size() % 2 != 0) {
        return false;
    }
    std::u16string units;
    for(std::size_t at = 2; at < text.size(); at += 2) {
        auto first = static_cast<unsigned char>(text[at]);
        auto second = static_cast<unsigned char>(text[at + 1]);
        units.push_back(static_cast<char16_t>(little ? first | second << 8 : first << 8 | second));
    }
    from_utf16(text, units);
    return true;
}

}  // namespace

split_view::iterator::iterator(std::string_view line, Reading reading) :
    rest(line), reading(reading), program(reading == Reading::Windows) {
    ++*this;
}

bool split_view::iterator::separates(char c) const {
    switch(reading) {
        case Reading::Windows: return c == ' ' || c == '\t';
        case Reading::WindowsFile:
            return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0';
        case Reading::POSIX: return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }
    std::unreachable();
}

split_view::iterator& split_view::iterator::operator++() {
    std::size_t at = 0;
    // The C runtime's program starts at the first character, a blank too.
    if(!program) {
        while(at < rest.size() && separates(rest[at])) {
            ++at;
        }
    }
    if(at == rest.size()) {
        done = true;
        return *this;
    }
    // The argument is a view of the line up to the first character to
    // unquote, if it ends before one.
    auto start = at;
    while(at < rest.size() && !separates(rest[at])) {
        char c = rest[at];
        if(c == '"' || (reading == Reading::POSIX && (c == '\'' || c == '\\'))) {
            break;
        }
        // Windows backslashes are literal but before a quote; a program's,
        // always.
        if(c == '\\' && !program) {
            auto end = rest.find_first_not_of('\\', at);
            if(end != std::string_view::npos && rest[end] == '"') {
                break;
            }
            at = end == std::string_view::npos ? rest.size() : end;
            continue;
        }
        ++at;
    }
    borrowed = at == rest.size() || separates(rest[at]);
    if(borrowed) {
        argument = rest.substr(start, at - start);
    } else {
        unquoted.assign(rest.substr(start, at - start));
        at = reading == Reading::POSIX ? unquote_posix(at) : unquote_windows(at);
    }
    program = false;
    rest.remove_prefix(at);
    return *this;
}

std::size_t split_view::iterator::unquote_windows(std::size_t at) {
    bool quoted = false;
    // It ends at a separator outside quotes, or at the end of the line, in
    // quotes or not.
    for(; at < rest.size() && (quoted || !separates(rest[at])); ++at) {
        char c = rest[at];
        if(c == '\\' && !program) {
            at = read_backslashes(rest, at, unquoted);
        } else if(c != '"') {
            unquoted.push_back(c);
        } else if(quoted && !program && at + 1 < rest.size() && rest[at + 1] == '"') {
            unquoted.push_back('"');
            ++at;
        } else {
            quoted = !quoted;
        }
    }
    return at;
}

std::size_t split_view::iterator::unquote_posix(std::size_t at) {
    // The quote the argument is in at `at`, if any.
    char quote = 0;
    for(; at < rest.size() && (quote != 0 || !separates(rest[at])); ++at) {
        char c = rest[at];
        if(quote == '\'') {
            if(c == '\'') {
                quote = 0;
            } else {
                unquoted.push_back(c);
            }
        } else if(c == '\\' && at + 1 < rest.size()) {
            unquoted.push_back(rest[++at]);
        } else if(quote != 0 && c == quote) {
            quote = 0;
        } else if(quote == 0 && (c == '\'' || c == '"')) {
            quote = c;
        } else {
            unquoted.push_back(c);
        }
    }
    return at;
}

void join(buffer_ref out, string_list_ref arguments, Quoting quoting) {
    out.clear();
    for(std::size_t i = 0; i < arguments.size(); ++i) {
        if(i != 0) {
            out.push_back(' ');
        }
        if(quoting == Quoting::POSIX) {
            append_posix(out, arguments[i]);
        } else if(i == 0) {
            append_windows_program(out, arguments[i]);
        } else {
            append_windows(out, arguments[i]);
        }
    }
}

expand_view::iterator::iterator(
    string_list_ref arguments,
    Quoting quoting,
    function_ref<std::error_code(buffer_ref out, std::string_view name)> read) :
    arguments(arguments), quoting(quoting), read(read) {
    files.reserve(max_depth);
    settle();
}

expand_view::iterator& expand_view::iterator::operator++() {
    advance();
    settle();
    return *this;
}

void expand_view::iterator::advance() {
    if(files.empty()) {
        ++index;
    } else {
        ++files.back().arguments;
    }
}

void expand_view::iterator::settle() {
    while(true) {
        if(!files.empty() && files.back().arguments == std::default_sentinel) {
            files.pop_back();
            advance();
            continue;
        }
        if(files.empty() && index == arguments.size()) {
            done = true;
            return;
        }
        argument = files.empty() ? arguments[index] : *files.back().arguments;
        if(!argument.starts_with('@') || files.size() == max_depth) {
            return;
        }
        std::string text;
        if(read(text, argument.substr(1)) || !decode(text)) {
            return;
        }
        files.push_back({std::move(text), {}});
        auto& file = files.back();
        using Reading = split_view::iterator::Reading;
        file.arguments = split_view::iterator(file.text,
                                              quoting == Quoting::Windows ? Reading::WindowsFile
                                                                          : Reading::POSIX);
    }
}

}  // namespace kotatsu::sys::cmd
