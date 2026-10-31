#pragma once

// LLVM's command-line tokenizers, which the tests of kotatsu::sys::cmd
// compare with: cl::TokenizeGNUCommandLine and cl::TokenizeWindowsCommandLine
// of llvm/lib/Support/CommandLine.cpp, and clang's CommandLineArgumentParser
// of clang/lib/Tooling/JSONCompilationDatabase.cpp, which reads a compilation
// database's commands on POSIX. Ported from LLVM 21 to std::string, without
// the marks of line ends that clang-cl asks for, and keeping what follows a
// NUL in an argument, which LLVM's C strings cut there.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace kotatsu::test {

namespace llvm_detail {

inline bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline bool is_whitespace_or_null(char c) {
    return is_whitespace(c) || c == '\0';
}

inline bool is_windows_special_char(char c) {
    return is_whitespace_or_null(c) || c == '\\' || c == '"';
}

inline std::size_t parse_backslash(std::string_view src, std::size_t i, std::string& token) {
    std::size_t e = src.size();
    std::size_t backslash_count = 0;
    do {
        ++i;
        ++backslash_count;
    } while(i != e && src[i] == '\\');

    bool followed_by_double_quote = i != e && src[i] == '"';
    if(followed_by_double_quote) {
        token.append(backslash_count / 2, '\\');
        if(backslash_count % 2 == 0) {
            return i - 1;
        }
        token.push_back('"');
        return i;
    }
    token.append(backslash_count, '\\');
    return i - 1;
}

/// clang's CommandLineArgumentParser.
class CommandLineArgumentParser {
public:
    explicit CommandLineArgumentParser(std::string_view command_line) : input(command_line) {}

    std::vector<std::string> parse() {
        bool has_more_input = true;
        while(has_more_input && next_non_whitespace()) {
            std::string argument;
            has_more_input = parse_string_into(argument);
            command_line.push_back(argument);
        }
        return command_line;
    }

private:
    // All private methods return true if there is more input available.

    bool parse_string_into(std::string& string) {
        do {
            if(at() == '"') {
                if(!parse_double_quoted_string_into(string)) {
                    return false;
                }
            } else if(at() == '\'') {
                if(!parse_single_quoted_string_into(string)) {
                    return false;
                }
            } else {
                if(!parse_free_string_into(string)) {
                    return false;
                }
            }
        } while(at() != ' ');
        return true;
    }

    bool parse_double_quoted_string_into(std::string& string) {
        if(!next()) {
            return false;
        }
        while(at() != '"') {
            if(!skip_escape_character()) {
                return false;
            }
            string.push_back(at());
            if(!next()) {
                return false;
            }
        }
        return next();
    }

    bool parse_single_quoted_string_into(std::string& string) {
        if(!next()) {
            return false;
        }
        while(at() != '\'') {
            string.push_back(at());
            if(!next()) {
                return false;
            }
        }
        return next();
    }

    bool parse_free_string_into(std::string& string) {
        do {
            if(!skip_escape_character()) {
                return false;
            }
            string.push_back(at());
            if(!next()) {
                return false;
            }
        } while(at() != ' ' && at() != '"' && at() != '\'');
        return true;
    }

    bool skip_escape_character() {
        if(at() == '\\') {
            return next();
        }
        return true;
    }

    bool next_non_whitespace() {
        do {
            if(!next()) {
                return false;
            }
        } while(at() == ' ');
        return true;
    }

    bool next() {
        ++position;
        return position != input.size();
    }

    char at() const {
        return input[position];
    }

    std::string_view input;

    /// Before the first character, as LLVM's Input.begin() - 1.
    std::size_t position = static_cast<std::size_t>(-1);

    std::vector<std::string> command_line;
};

}  // namespace llvm_detail

/// The arguments cl::TokenizeGNUCommandLine reads in `src`.
inline std::vector<std::string> tokenize_gnu_command_line(std::string_view src) {
    using namespace llvm_detail;
    std::vector<std::string> new_argv;
    std::string token;
    bool in_token = false;
    for(std::size_t i = 0, e = src.size(); i != e; ++i) {
        // Consume runs of whitespace.
        if(!in_token) {
            while(i != e && is_whitespace(src[i])) {
                ++i;
            }
            if(i == e) {
                break;
            }
            in_token = true;
        }

        char c = src[i];

        // Backslash escapes the next character.
        if(i + 1 < e && c == '\\') {
            ++i;  // Skip the escape.
            token.push_back(src[i]);
            continue;
        }

        // Consume a quoted string.
        if(c == '"' || c == '\'') {
            ++i;
            while(i != e && src[i] != c) {
                // Backslash escapes the next character.
                if(src[i] == '\\' && i + 1 != e) {
                    ++i;
                }
                token.push_back(src[i]);
                ++i;
            }
            if(i == e) {
                break;
            }
            continue;
        }

        // End the token if this is whitespace.
        if(is_whitespace(c)) {
            new_argv.push_back(token);
            token.clear();
            in_token = false;
            continue;
        }

        // This is a normal character. Append it.
        token.push_back(c);
    }

    // Append the last token after hitting EOF with no whitespace.
    if(in_token) {
        new_argv.push_back(token);
    }
    return new_argv;
}

/// The arguments cl::TokenizeWindowsCommandLine reads in `src`.
inline std::vector<std::string> tokenize_windows_command_line(std::string_view src) {
    using namespace llvm_detail;
    std::vector<std::string> new_argv;
    std::string token;

    // Try to do as much work inside the state machine as possible.
    enum { Init, Unquoted, Quoted } state = Init;

    for(std::size_t i = 0, e = src.size(); i < e; ++i) {
        switch(state) {
            case Init: {
                // Eat whitespace before a token.
                while(i < e && is_whitespace_or_null(src[i])) {
                    ++i;
                }
                // Stop if this was trailing whitespace.
                if(i >= e) {
                    break;
                }
                std::size_t start = i;
                while(i < e && !is_windows_special_char(src[i])) {
                    ++i;
                }
                auto normal_chars = src.substr(start, i - start);
                if(i >= e || is_whitespace_or_null(src[i])) {
                    // No special characters: slice out the substring and
                    // start the next token.
                    new_argv.emplace_back(normal_chars);
                } else if(src[i] == '"') {
                    token += normal_chars;
                    state = Quoted;
                } else {
                    token += normal_chars;
                    i = parse_backslash(src, i, token);
                    state = Unquoted;
                }
                break;
            }

            case Unquoted:
                if(is_whitespace_or_null(src[i])) {
                    // Whitespace means the end of the token.
                    new_argv.push_back(token);
                    token.clear();
                    state = Init;
                } else if(src[i] == '"') {
                    state = Quoted;
                } else if(src[i] == '\\') {
                    i = parse_backslash(src, i, token);
                } else {
                    token.push_back(src[i]);
                }
                break;

            case Quoted:
                if(src[i] == '"') {
                    if(i < (e - 1) && src[i + 1] == '"') {
                        // Consecutive double-quotes inside a quoted string
                        // implies one double-quote.
                        token.push_back('"');
                        ++i;
                    } else {
                        // Otherwise, end the quoted portion and return to the
                        // unquoted state.
                        state = Unquoted;
                    }
                } else if(src[i] == '\\') {
                    i = parse_backslash(src, i, token);
                } else {
                    token.push_back(src[i]);
                }
                break;
        }
    }

    if(state != Init) {
        new_argv.push_back(token);
    }
    return new_argv;
}

/// The arguments clang reads in a compilation database's command `command`
/// on POSIX.
inline std::vector<std::string> unescape_json_command_line(std::string_view command) {
    return llvm_detail::CommandLineArgumentParser(command).parse();
}

}  // namespace kotatsu::test
