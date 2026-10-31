#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "sys/harness/llvm_tokenizers.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/support/buffer_ref.h"
#include "kotatsu/sys/cmd.h"

// cmd's readings against the LLVM tokenizers they follow, on every string up
// to 5 characters long of the characters that matter to them, and on longer
// ones drawn at random.

namespace kotatsu::sys::cmd {

namespace {

using namespace std::literals;

using Arguments = std::vector<std::string>;

/// The arguments split() reads in `line`.
Arguments split(std::string_view line, Quoting quoting) {
    Arguments arguments;
    for(auto argument: cmd::split(line, quoting)) {
        arguments.emplace_back(argument);
    }
    return arguments;
}

/// The arguments expand() reads in a response file of `text`.
Arguments read_file(std::string_view text, Quoting quoting) {
    auto read = [&](buffer_ref out, std::string_view) {
        out.assign(text);
        return std::error_code();
    };
    std::string_view arguments[] = {"@file"};
    Arguments result;
    for(auto argument: expand(arguments, quoting, read)) {
        result.emplace_back(argument);
    }
    return result;
}

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
    constexpr std::uint32_t seed = 20261031;
    ZEST_CONTEXT("seed {}", seed);
    std::mt19937 random(seed);
    std::uniform_int_distribution<std::size_t> lengths(6, 40);
    std::uniform_int_distribution<std::size_t> characters(0, alphabet.size() - 1);
    for(int run = 0; run < 5000; ++run) {
        input.clear();
        for(auto length = lengths(random); length != 0; --length) {
            input.push_back(alphabet[characters(random)]);
        }
        check(input);
    }
}

ZEST_SUITE(sys_cmd_llvm) {

// Without single quotes, in which LLVM's GNU tokenizer takes escapes.
ZEST_CASE(posix_reads_as_llvms_gnu_tokenizer) {
    compare(
        "ab \t\r\n\0\"\\"sv,
        [](std::string_view line) { return split(line, Quoting::POSIX); },
        test::tokenize_gnu_command_line);
}

// As clang reads a compilation database's command on POSIX, but for tabs and
// line ends, which it does not separate at, and a backslash at the end,
// which it drops.
ZEST_CASE(posix_reads_as_clangs_database) {
    compare(
        "ab \0\"'\\"sv,
        [](std::string_view line) { return split(line, Quoting::POSIX); },
        test::unescape_json_command_line,
        [](std::string_view line) { return !line.ends_with('\\'); });
}

// The arguments after a program, which LLVM reads as the C runtime does but
// that line ends and NULs separate them too.
ZEST_CASE(windows_reads_arguments_as_llvms_windows_tokenizer) {
    compare(
        "ab \t\"\\"sv,
        [](std::string_view line) { return split("p "s.append(line), Quoting::Windows); },
        [](std::string_view line) {
            return test::tokenize_windows_command_line("p "s.append(line));
        });
}

ZEST_CASE(a_windows_file_reads_as_llvms_windows_tokenizer) {
    compare(
        "ab \t\r\n\0\"\\"sv,
        [](std::string_view text) { return read_file(text, Quoting::Windows); },
        test::tokenize_windows_command_line);
}

ZEST_CASE(a_posix_file_reads_as_a_posix_line) {
    compare(
        "ab \t\r\n\0\"'\\"sv,
        [](std::string_view text) { return read_file(text, Quoting::POSIX); },
        [](std::string_view line) { return split(line, Quoting::POSIX); });
}

};  // ZEST_SUITE(sys_cmd_llvm)

}  // namespace

}  // namespace kotatsu::sys::cmd
