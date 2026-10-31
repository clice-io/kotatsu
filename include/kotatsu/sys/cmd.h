#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "kotatsu/support/buffer_ref.h"
#include "kotatsu/support/functional.h"
#include "kotatsu/support/small_string.h"
#include "kotatsu/support/string_list_ref.h"

// Command lines, of either style on every host: a Windows program's read
// elsewhere, a build's commands, response files and what compilers print.

namespace kotatsu::sys::cmd {

/// How a command line quotes its arguments.
enum class Quoting : std::uint8_t {
    /// A Windows program's, as CreateProcess passes it and the C runtime
    /// splits it into argv (its rules since 2008): the program first, ending
    /// at the first space or tab outside quotes, each quote in it toggling
    /// them and backslashes literal; after it, spaces and tabs outside quotes
    /// separate arguments, 2n backslashes before a quote make n and the quote
    /// toggles, 2n+1 make n and a literal quote, two quotes inside quotes
    /// make one, and other backslashes are literal. A line that starts with
    /// a blank has an empty program.
    Windows,
    /// A POSIX shell's, without its expansions: spaces, tabs, carriage
    /// returns and newlines outside quotes separate arguments; single quotes
    /// keep all up to the next one as written; elsewhere a backslash escapes
    /// the character after it, inside double quotes too, one at the end
    /// being literal. A quote left open runs to the end.
    POSIX,
};

class expand_view;

/// The arguments of a command line, as split() reads them: a view that
/// reads the line as it goes.
class split_view : public std::ranges::view_interface<split_view> {
public:
    class iterator {
    public:
        using iterator_concept = std::input_iterator_tag;
        using value_type = std::string_view;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        /// The argument, which holds until this iterator moves on or goes.
        std::string_view operator*() const {
            return borrowed ? argument : std::string_view(unquoted);
        }

        iterator& operator++();

        void operator++(int) {
            ++*this;
        }

        friend bool operator==(const iterator& it, std::default_sentinel_t) {
            return it.done;
        }

    private:
        friend class split_view;
        friend class expand_view;

        /// How the line is read: as Quoting says, or as clang-cl reads a
        /// Windows response file, which has no program and in which
        /// carriage returns, newlines and NULs separate arguments too.
        enum class Reading : std::uint8_t { Windows, WindowsFile, POSIX };

        iterator(std::string_view line, Reading reading);

        /// Whether `c` separates arguments outside quotes.
        bool separates(char c) const;

        /// Reads into `unquoted` the rest of the argument from `at`, where
        /// the first character to unquote is. The index past it.
        std::size_t unquote_windows(std::size_t at);
        std::size_t unquote_posix(std::size_t at);

        /// What is left of the line after the argument.
        std::string_view rest;

        /// The argument when it is a view of the line.
        std::string_view argument;

        /// The argument when it had to be unquoted.
        small_string<256> unquoted;

        Reading reading = Reading::POSIX;

        /// Whether the next argument is a Windows program, read by its own
        /// rule.
        bool program = false;

        /// Whether the argument is `argument` rather than `unquoted`.
        bool borrowed = true;

        /// Whether the argument is past the last.
        bool done = false;
    };

    split_view(std::string_view line, Quoting quoting) : line(line), quoting(quoting) {}

    iterator begin() const {
        using Reading = iterator::Reading;
        return iterator(line, quoting == Quoting::Windows ? Reading::Windows : Reading::POSIX);
    }

    std::default_sentinel_t end() const {
        return {};
    }

private:
    std::string_view line;

    Quoting quoting;
};

/// The arguments of command line `line`, quoted as `quoting` says. One with
/// nothing to unquote is a view of `line`, the others are unquoted into the
/// iterator; either holds until the iterator moves on.
inline split_view split(std::string_view line, Quoting quoting) {
    return split_view(line, quoting);
}

/// Writes into `out` the command line of `arguments`, quoted for split() to
/// read them back. Windows: the program as it is, in quotes when it is empty
/// or holds a space or tab (it holds no quote, as no Windows file name
/// does); each argument after it as it is when it is not empty and holds no
/// blank, newline or quote, else in quotes, each quote escaped and the
/// backslashes before a quote or the closing one doubled. POSIX: a
/// backslash before each ASCII character that is not a letter, a digit or
/// one of `@%+=:,./_-`, a newline in single quotes instead, which sh would
/// take for a line continuation, and an empty argument as `''`; sh reads it
/// back alike.
void join(buffer_ref out, string_list_ref arguments, Quoting quoting);

/// The arguments of a command with its response files read, as expand()
/// gives them: a view that reads them as it goes.
class expand_view : public std::ranges::view_interface<expand_view> {
public:
    /// An iterator that cannot be copied: the arguments it views are in the
    /// response files it holds.
    class iterator {
    public:
        using iterator_concept = std::input_iterator_tag;
        using value_type = std::string_view;
        using difference_type = std::ptrdiff_t;

        iterator(iterator&&) = default;

        iterator& operator=(iterator&&) = default;

        /// The argument, which holds until this iterator moves on or goes.
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
        friend class expand_view;

        /// How deep response files are read in one another.
        constexpr static std::size_t max_depth = 8;

        /// A response file being read: its text, and its arguments from the
        /// one at hand on.
        struct File {
            std::string text;

            split_view::iterator arguments;
        };

        iterator(string_list_ref arguments,
                 Quoting quoting,
                 function_ref<std::error_code(buffer_ref out, std::string_view name)> read);

        /// Moves past the argument at hand, in the innermost file or in the
        /// command.
        void advance();

        /// Moves on from where it is to the first argument that is no
        /// response file to read, reading those before it.
        void settle();

        string_list_ref arguments;

        Quoting quoting;

        function_ref<std::error_code(buffer_ref out, std::string_view name)> read;

        /// The argument of the command at hand, or holding the files.
        std::size_t index = 0;

        /// The response files being read, the innermost last. Never more
        /// than max_depth, which it holds room for from the start, so that a
        /// file's place, that its arguments view, stays.
        std::vector<File> files;

        std::string_view argument;

        bool done = false;
    };

    expand_view(string_list_ref arguments,
                Quoting quoting,
                function_ref<std::error_code(buffer_ref out, std::string_view name)> read) :
        arguments(arguments), quoting(quoting), read(read) {}

    iterator begin() const {
        return iterator(arguments, quoting, read);
    }

    std::default_sentinel_t end() const {
        return {};
    }

private:
    string_list_ref arguments;

    Quoting quoting;

    function_ref<std::error_code(buffer_ref out, std::string_view name)> read;
};

/// The arguments of command `arguments`, each `@name` among them replaced by
/// those of the response file `read` writes into `out`, empty, for `name`,
/// unless it returns an error, which leaves `@name` as written: as GCC and
/// clang expand them, which resolve a relative `name` against the directory
/// the command runs in, as `read` does itself. A file's text has a UTF-8
/// byte order mark dropped and UTF-16 with one converted, and is split as
/// `quoting` says, but that, as clang-cl reads a Windows one, none of its
/// arguments is a program and carriage returns, newlines and NULs separate
/// them too. UTF-16 of an odd number of bytes leaves `@name` as written. An
/// `@name` in a file is expanded alike, up to 8 files deep, past which it
/// stays as written. Each argument holds until the iterator moves on;
/// `arguments` and `read` must outlive the view.
inline expand_view
    expand(string_list_ref arguments,
           Quoting quoting,
           function_ref<std::error_code(buffer_ref out, std::string_view name)> read) {
    return expand_view(arguments, quoting, read);
}

}  // namespace kotatsu::sys::cmd
