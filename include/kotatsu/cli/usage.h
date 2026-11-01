#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "kotatsu/cli/error.h"
#include "kotatsu/cli/parse.h"

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
};

namespace detail {

std::vector<UsageEntry> usage_entries(const Command& command);

std::string usage(const Command& command, std::string_view usage_line, const Style& style);

}  // namespace detail

/// The entries a usage of the options `T` declares lists, in declaration order: what a program
/// lays out its own help from.
template <typename T>
std::vector<UsageEntry> usage_entries() {
    return detail::usage_entries(detail::command_of<T>());
}

/// The usage of a program taking the options `T` declares: what `T` is (its static
/// `description`), `usage: <usage_line>`, its options, by group, and its subcommands.
template <typename T>
std::string usage(std::string_view usage_line, const Style& style = {}) {
    return detail::usage(detail::command_of<T>(), usage_line, style);
}

}  // namespace kotatsu::cli
