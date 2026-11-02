#pragma once

#include <cassert>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "kotatsu/cli/error.h"
#include "kotatsu/cli/parse.h"
#include "kotatsu/cli/to_argv.h"
#include "kotatsu/support/string_list_ref.h"

namespace kotatsu::cli {

/// An option, positional argument or subcommand as a usage lists it.
struct UsageEntry {
    enum class Kind : std::uint8_t {
        Option,
        /// A positional argument, or the arguments after "--".
        Positional,
        Subcommand,
    };

    Kind kind = Kind::Option;
    /// How it is written: "-o|--output <FILE>", "<PATH>...", "-- <ARG>...", "serve".
    std::string usage = {};
    /// Its help: the field's description, or the subcommand struct's.
    std::string help = {};
    /// The heading it is listed under, the description of the group it is in; empty for the
    /// command's own.
    std::string group = {};
    /// Whether every occurrence of it counts, which its help then says.
    bool repeatable = false;
    /// The value it has when the argv leaves it out, as an argv writes it: "0", "a, b". Empty
    /// for none: a required one, a flag, a count, an alias, an empty list, an empty optional,
    /// an option of an alternative a default-constructed struct does not hold, a value that
    /// does not write (an enum value without a name), and all of a struct that is not
    /// default-constructible.
    std::string default_value = {};
    /// The numbers it takes, from its field's minimum and maximum: "1..64", "1..", "..64";
    /// empty when unbounded.
    std::string range = {};
};

namespace detail {

/// The options naming `subcommands`, outermost first, under `command`, and nothing more: what
/// a decode reads into options that hold those subcommands and their defaults.
Node subcommand_path(const Command& command, string_list_ref subcommands);

/// The options a default-constructed `T` holds once it holds `subcommands`, which a usage says
/// the defaults of; none for a `T` that is not default-constructible, or a field whose value
/// does not write.
template <typename T>
Node defaults_of(string_list_ref subcommands, std::deque<std::string>& texts) {
    if constexpr(std::is_default_constructible_v<T>) {
        const Node path = subcommand_path(command_of<T>(), subcommands);
        T options{};
        const Node* last = nullptr;
        codec::rich_error error;
        codec::scoped_context<codec::rich_error> guard(error);
        Reader reader{path, &last};
        [[maybe_unused]] const bool decoded = codec::decode_value(reader, options);
        assert(decoded);
        return write_options(options, texts, true);
    } else {
        return Node{};
    }
}

std::vector<UsageEntry> usage_entries(const Command& command, const Node& defaults);

std::string usage(const Command& command,
                  const Node& defaults,
                  std::string_view program,
                  string_list_ref subcommands,
                  const Style& style);

}  // namespace detail

/// The entries a usage of the options `T` declares lists, the help option first, then in
/// declaration order: what a program lays out its own help from.
template <typename T>
std::vector<UsageEntry> usage_entries() {
    std::deque<std::string> texts;
    return detail::usage_entries(detail::command_of<T>(), detail::defaults_of<T>({}, texts));
}

/// The usage of the command `subcommands` names, outermost first, in a program `program` taking
/// the options `T`; of the program's own when they are none, and `subcommands` of a Help asks
/// for the one it was asked for. It says what the command is (the description its struct's
/// KANNOTATE_TYPE gives), the line that calls it, "usage: tool serve <PATH> [OPTIONS]", its
/// options, by group, and its subcommands. The line writes the program's name and the
/// subcommands' words, then the command's positional arguments and the options it requires, in
/// declaration order, then "[OPTIONS]", the arguments after "--", and a placeholder of the
/// subcommand it takes: the meta_var of its field, else "<command>". What may be left out is in
/// brackets, and an exclusive field is its alternatives, "(<LOCATOR> | --spec <FILE>)", when it
/// is required or one of them takes positional arguments. `subcommands` must name subcommands
/// of `T`.
template <typename T>
std::string usage(std::string_view program,
                  string_list_ref subcommands = {},
                  const Style& style = {}) {
    std::deque<std::string> texts;
    return detail::usage(detail::command_of<T>(),
                         detail::defaults_of<T>(subcommands, texts),
                         program,
                         subcommands,
                         style);
}

}  // namespace kotatsu::cli
