#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "kotatsu/cli/error.h"
#include "kotatsu/cli/parse.h"
#include "kotatsu/cli/to_argv.h"

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

/// The options a default-constructed `T` holds, which a usage says the defaults of; none for a
/// `T` that is not default-constructible, or a field whose value does not write.
template <typename T>
Node defaults_of(std::deque<std::string>& texts) {
    if constexpr(std::is_default_constructible_v<T>) {
        return write_options(T{}, texts, true);
    } else {
        return Node{};
    }
}

std::vector<UsageEntry> usage_entries(const Command& command, const Node& defaults);

std::string usage(const Command& command,
                  const Node& defaults,
                  std::string_view usage_line,
                  const Style& style);

}  // namespace detail

/// The entries a usage of the options `T` declares lists, in declaration order: what a program
/// lays out its own help from.
template <typename T>
std::vector<UsageEntry> usage_entries() {
    std::deque<std::string> texts;
    return detail::usage_entries(detail::command_of<T>(), detail::defaults_of<T>(texts));
}

/// The usage of a program taking the options `T` declares: what `T` is (its static
/// `description`), `usage: <usage_line>`, its options, by group, and its subcommands.
template <typename T>
std::string usage(std::string_view usage_line, const Style& style = {}) {
    std::deque<std::string> texts;
    return detail::usage(detail::command_of<T>(), detail::defaults_of<T>(texts), usage_line, style);
}

}  // namespace kotatsu::cli
