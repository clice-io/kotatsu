#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kotatsu::cli {

/// How a usage and an error are laid out.
struct Style {
    /// The modern look, in ANSI colors: headings without colons, groups marked with a diamond,
    /// and an error drawn under its argv with box-drawing characters.
    bool color = false;
    /// The column an option's help starts at; an option reaching it puts its help on the next
    /// line.
    std::size_t help_column = 32;
    /// The width an error's argv line is cut to, around the arguments it marks; 0 keeps it
    /// whole.
    std::size_t argv_width = 96;
};

/// Why an argv does not parse.
struct Error {
    enum class Kind : std::uint8_t {
        /// An option nothing declares: `--bogus`.
        UnknownOption,
        /// An option without the value it takes: `-o` at the end.
        MissingValue,
        /// A value its field cannot take: `--jobs many`, or one a repr refuses.
        InvalidValue,
        /// A positional argument nothing takes.
        UnexpectedArgument,
        /// A required option or positional argument left out.
        MissingRequired,
        /// Arguments of two alternatives of a cli::exclusive field: `--spec` beside `<LOCATOR>`.
        Conflict,
        /// A word where a subcommand goes that names none.
        UnknownSubcommand,
        /// No subcommand where one is required.
        MissingSubcommand,
    };

    Kind kind;
    /// One line: "invalid integer value: many".
    std::string message;
    /// The argv the error is in.
    std::vector<std::string> argv;
    /// The elements of `argv` at fault, [begin, end); `begin == argv.size()` is its end.
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    /// The subcommands the argv names before the elements at fault, outermost first:
    /// {"index", "stats"}; empty for an error in the options' own arguments.
    std::vector<std::string> subcommands = {};
    /// For an unknown option or subcommand, the name of the command's closest to it, which a
    /// rendering offers: "--verbose" for "--verbos"; empty when none is close.
    std::string suggestion = {};

    /// `message` under the argv, the elements at fault marked, as a program reports it:
    ///
    ///     at argv[1]:
    ///       --jobs many
    ///              ^~~~
    ///       invalid integer value: many
    ///
    /// The suggestion follows the message: "...; did you mean '--verbose'?". For an empty
    /// argv, the message and the suggestion alone. In color, it reads "error [argv[1]]" and draws
    /// the mark.
    std::string render(const Style& style = {}) const;
};

}  // namespace kotatsu::cli
