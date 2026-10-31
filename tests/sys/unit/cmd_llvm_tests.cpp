#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "sys/harness/cmd.h"
#include "sys/harness/llvm_tokenizers.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/sys/cmd.h"

// cmd's readings against the LLVM tokenizers they follow, on every string up
// to 5 characters long of the characters that matter to them, and on longer
// ones drawn at random.

namespace kotatsu::sys::cmd {

namespace {

using namespace std::literals;

using Arguments = std::vector<std::string>;

using test::read_response_file;
using test::split_all;

/// `text` with each byte that is not printable ASCII as `\xNN`.
std::string escaped(std::string_view text) {
    std::string out;
    for(char c: text) {
        auto byte = static_cast<unsigned char>(c);
        out += byte >= 0x20 && byte < 0x7F ? std::string(1, c) : std::format("\\x{:02X}", byte);
    }
    return out;
}

/// Checks that `ours` and `theirs` read alike each string of `alphabet` that
/// `compared` takes: every one up to 5 long, and longer ones drawn at random.
void compare(
    std::string_view alphabet,
    std::function<Arguments(std::string_view)> ours,
    std::function<Arguments(std::string_view)> theirs,
    std::function<bool(std::string_view)> compared = [](std::string_view) { return true; }) {
    auto check = [&](const std::string& input) {
        if(!compared(input)) {
            return;
        }
        ZEST_CONTEXT("input: {}", escaped(input));
        ZASSERT(ours(input) == theirs(input));
    };
    std::string input;
    std::vector<std::size_t> digits;
    for(std::size_t length = 0; length <= 5; ++length) {
        digits.assign(length, 0);
        while(true) {
            input.clear();
            for(auto digit: digits) {
                input.push_back(alphabet[digit]);
            }
            check(input);
            // The next string of this length, the last character first.
            std::size_t at = length;
            while(at != 0 && ++digits[at - 1] == alphabet.size()) {
                digits[--at] = 0;
            }
            if(at == 0) {
                break;
            }
        }
    }
    // Drawn alike with every standard library.
    constexpr std::uint32_t seed = 20261031;
    ZEST_CONTEXT("seed {}", seed);
    std::mt19937 random(seed);
    for(int run = 0; run < 5000; ++run) {
        input.clear();
        for(auto length = 6 + random() % 35; length != 0; --length) {
            input.push_back(alphabet[random() % alphabet.size()]);
        }
        check(input);
    }
}

/// `line` with each carriage return, newline and NUL as a byte LLVM's Windows
/// tokenizer reads as any other, or back.
std::string swap_line_ends(std::string_view line) {
    std::string out(line);
    for(auto& c: out) {
        constexpr std::string_view ends("\r\n\0", 3);
        constexpr std::string_view others("\x01\x02\x03", 3);
        if(auto at = ends.find(c); at != std::string_view::npos) {
            c = others[at];
        } else if(auto at = others.find(c); at != std::string_view::npos) {
            c = ends[at];
        }
    }
    return out;
}

ZEST_SUITE(sys_cmd_llvm) {

// But that LLVM's GNU tokenizer takes escapes in single quotes.
ZEST_CASE(posix_reads_as_llvms_gnu_tokenizer) {
    compare(
        "ab \t\r\n\0\"'\\"sv,
        [](std::string_view line) { return split_all(line, Quoting::POSIX); },
        test::tokenize_gnu_command_line,
        [](std::string_view line) { return !line.contains('\'') || !line.contains('\\'); });
}

// As clang reads a compilation database's command on POSIX, but for tabs and
// line ends, which it does not separate at, and an odd run of backslashes at
// the end, the last of which it drops.
ZEST_CASE(posix_reads_as_clangs_database) {
    compare(
        "ab \0\"'\\"sv,
        [](std::string_view line) { return split_all(line, Quoting::POSIX); },
        test::unescape_json_command_line,
        [](std::string_view line) {
            auto backslashes = line.size() - (line.find_last_not_of('\\') + 1);
            return backslashes % 2 == 0;
        });
}

// The arguments after a program, which LLVM reads as the C runtime does, but
// that line ends and NULs separate them too: swapped for other bytes, which
// it reads alike.
ZEST_CASE(windows_reads_arguments_as_llvms_windows_tokenizer) {
    compare(
        "ab \t\r\n\0\"\\"sv,
        [](std::string_view line) { return split_all("p "s.append(line), Quoting::Windows); },
        [](std::string_view line) {
            Arguments arguments;
            for(auto& argument:
                test::tokenize_windows_command_line(swap_line_ends("p "s.append(line)))) {
                arguments.push_back(swap_line_ends(argument));
            }
            return arguments;
        });
}

ZEST_CASE(a_windows_file_reads_as_llvms_windows_tokenizer) {
    compare(
        "ab \t\r\n\0\"\\"sv,
        [](std::string_view text) { return read_response_file(text, Quoting::Windows); },
        test::tokenize_windows_command_line);
}

};  // ZEST_SUITE(sys_cmd_llvm)

}  // namespace

}  // namespace kotatsu::sys::cmd
