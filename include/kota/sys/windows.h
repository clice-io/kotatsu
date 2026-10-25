#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "kota/support/buffer_ref.h"
#include "kota/support/small_string.h"
#include "kota/support/string_list_ref.h"

// What only Windows has. The functions of strings and numbers are declared on
// every host, for Windows command lines and exit codes read elsewhere; those
// that ask Windows itself only on Windows.

namespace kota::sys::windows {

/// Writes into `out` `argument` as a Windows command line writes it, for the
/// C runtime's rules (CommandLineRules::CRT) to read back: as it is when it
/// is not empty and holds no space, tab, newline, vertical tab or quote; else
/// in quotes, with each quote escaped and the backslashes before a quote or
/// the closing one doubled.
void quote_argument(buffer_ref out, std::string_view argument);

/// Writes into `out` the command line of `arguments`, the program first. The
/// program is read by its own rule, in which a backslash escapes nothing: it
/// is quoted when it is empty or holds a space or a tab, and holds no quote,
/// which no Windows file name does. Each argument after it is quoted by
/// quote_argument.
void join_command_line(buffer_ref out, string_list_ref arguments);

/// The rules a command line is read by.
enum class CommandLineRules : std::uint8_t {
    /// The C runtime's since 2008, which a program reads its own by (Rust's
    /// parser): the program ends at the first space or tab outside quotes,
    /// every quote in it toggling them; after it, spaces and tabs outside
    /// quotes separate arguments, 2n backslashes before a quote make n and
    /// the quote toggles, 2n+1 make n and a literal quote, two quotes inside
    /// quotes make one, and other backslashes are literal. An empty line has
    /// no arguments, and one that starts with a blank an empty program.
    CRT,
    /// LLVM's cl::TokenizeWindowsCommandLine, by which clang reads a
    /// compilation database's commands and a clang-cl response file (only
    /// clang-cl, or --rsp-quoting=windows, reads response files so): each
    /// argument by the CRT's rules for those after the program, none being
    /// the program, but that carriage returns, newlines and NULs separate
    /// them too, and that separators before the first make no empty one.
    LLVM,
    /// LLVM's cl::TokenizeWindowsCommandLineFull, by which an LLVM tool reads
    /// its own command line on Windows: LLVM's rules, but that the first
    /// argument, and each after one that a newline ends, is a program, read
    /// without escapes, two quotes inside quotes still making one.
    LLVMFull,
};

/// The arguments of a command line, as split_command_line() gives them: a
/// view that reads the line as it goes, each argument a view of the
/// iterator, which holds until that iterator moves on or goes.
class split_command_line_view : public std::ranges::view_interface<split_command_line_view> {
public:
    class iterator {
    public:
        using iterator_concept = std::input_iterator_tag;
        using value_type = std::string_view;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        std::string_view operator*() const {
            return argument;
        }

        iterator& operator++();

        void operator++(int) {
            ++*this;
        }

        friend bool operator==(const iterator& it, std::default_sentinel_t) {
            return it.done;
        }

    private:
        friend class split_command_line_view;

        iterator(std::string_view line, CommandLineRules rules);

        /// What is left of the line after the argument.
        std::string_view rest;

        /// The argument, its quotes and escapes read.
        small_string<256> argument;

        /// The rules the line is read by.
        CommandLineRules rules = CommandLineRules::CRT;

        /// Whether the next argument is a program, read by its own rule.
        bool program = true;

        /// Whether the argument is past the last.
        bool done = false;
    };

    split_command_line_view() = default;

    explicit split_command_line_view(std::string_view line,
                                     CommandLineRules rules = CommandLineRules::CRT) :
        line(line), rules(rules) {}

    iterator begin() const {
        return iterator(line, rules);
    }

    std::default_sentinel_t end() const {
        return {};
    }

private:
    std::string_view line;

    CommandLineRules rules = CommandLineRules::CRT;
};

/// The arguments of command line `line`, read by `rules`. No wildcard is
/// expanded.
inline split_command_line_view split_command_line(std::string_view line,
                                                  CommandLineRules rules = CommandLineRules::CRT) {
    return split_command_line_view(line, rules);
}

/// The name of NTSTATUS `status` ("STATUS_DLL_NOT_FOUND" for 0xC0000135),
/// among those a program ends with when it crashes or cannot start; empty for
/// another.
std::string_view ntstatus_name(std::uint32_t status);

#ifdef _WIN32

/// The directories CreateProcess searches for a program its command line
/// names without a path: the directory of this program's executable, the
/// current directory, the system directory, the 16-bit system directory, the
/// Windows directory, then PATH.
std::expected<std::vector<std::string>, std::error_code> create_process_directories();

/// Writes into `out` the file CreateProcess runs for program `name` from
/// `directories`, by its rules: a name with a separator or a drive is a
/// path, looked for nowhere else; `.exe` is added only to a name without a
/// dot, which CreateProcess takes for an extension of its own, a leading one
/// (`.bat`) too; and a Cygwin symlink is the file it is, which
/// CreateProcess cannot start. Errors, and what `out` names on
/// permission_denied, as find_program's.
std::error_code find_create_process_program(buffer_ref out,
                                            std::string_view name,
                                            string_list_ref directories);

#endif

}  // namespace kota::sys::windows
